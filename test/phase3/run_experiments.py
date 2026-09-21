#!/usr/bin/env python3
"""Run real transfers on the two existing Vagrant VMs; never starts the VMs.

Single-run defaults preserve networking. Matrix / network flags explicitly opt
into changing both VM egress qdiscs and restoring the documented baseline.
"""
import argparse
import concurrent.futures
import datetime as dt
import json
import math
import os
from pathlib import Path
import shlex
import signal
import sys
import time
import uuid


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--client-port", type=int, default=2222)
    parser.add_argument("--server-port", type=int, default=2200)
    parser.add_argument("--user", default="vagrant")
    parser.add_argument("--password-env", default="TJU_SSH_PASSWORD",
                        help="environment variable holding SSH password (fallback: vagrant)")
    parser.add_argument("--known-hosts", help="known_hosts file; default is explicit ephemeral-VM trust")
    parser.add_argument("--repo", default="/vagrant/tju_tcp")
    parser.add_argument("--output", type=Path, default=Path(__file__).parent / "results")
    parser.add_argument("--bytes", type=int, default=100 * 1024 * 1024)
    parser.add_argument("--timeout", type=int, default=600)
    parser.add_argument("--mode", choices=("off", "basic", "reno", "newreno", "cubic"), default="basic")
    parser.add_argument("--build", action="store_true", help="build production and phase3 binaries on client VM first")
    parser.add_argument("--recv-capacity", type=int)
    parser.add_argument("--read-delay-ms", type=int, default=0)
    parser.add_argument("--start-delay-ms", type=int, default=0)
    parser.add_argument("--checksum", action="store_true", help="enable optional packet checksum extension")
    parser.add_argument("--rack", action="store_true", help="enable optional RACK-TLP timer extension")
    parser.add_argument("--sack", action="store_true", help="enable optional SACK scoreboard extension")
    parser.add_argument("--interface", default="enp0s8")
    parser.add_argument("--loss", type=float, help="egress loss percent on BOTH VMs; opts into tc changes")
    parser.add_argument("--delay-ms", type=float, help="one-way egress delay on EACH VM; opts into tc changes")
    parser.add_argument("--rate-mbps", type=float, help="egress rate on BOTH VMs; opts into tc changes")
    parser.add_argument("--baseline-rate-mbps", type=float, default=100)
    parser.add_argument("--baseline-delay-ms", type=float, default=20)
    parser.add_argument("--baseline-loss", type=float, default=0)
    parser.add_argument("--matrix", action="store_true", help="vary loss and delay separately; modifies VM qdiscs")
    parser.add_argument("--modes", nargs="+", choices=("off", "basic", "reno", "newreno", "cubic"),
                        default=["basic", "reno", "newreno"])
    parser.add_argument("--loss-values", nargs="+", type=float, default=[0, 1, 3, 5])
    parser.add_argument("--delay-values", nargs="+", type=float, default=[10, 20, 50, 100])
    parser.add_argument("--repetitions", type=int, default=3)
    parser.add_argument("--continue-on-failure", action="store_true")
    args = parser.parse_args()
    if not 0 < args.bytes <= 1024 * 1024 * 1024:
        parser.error("--bytes must be between 1 and 1073741824")
    if not 0 < args.timeout <= 86400 or args.repetitions < 1:
        parser.error("invalid timeout or repetition count")
    for value in [args.loss, args.baseline_loss] + args.loss_values:
        if value is not None and (not math.isfinite(value) or not 0 <= value <= 100):
            parser.error("loss values must be in [0, 100]")
    for value in [args.delay_ms, args.baseline_delay_ms] + args.delay_values:
        if value is not None and (not math.isfinite(value) or value < 0):
            parser.error("delay must be nonnegative")
    for value in [args.rate_mbps, args.baseline_rate_mbps]:
        if value is not None and (not math.isfinite(value) or value <= 0):
            parser.error("rate must be positive")
    return args


def write_json(path, data):
    path.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


class Remote:
    def __init__(self, paramiko, args, role, port):
        self.role = role
        self.ssh = paramiko.SSHClient()
        if args.known_hosts:
            self.ssh.load_host_keys(args.known_hosts)
            self.ssh.set_missing_host_key_policy(paramiko.RejectPolicy())
        else:
            # These are local disposable Vagrant guests; no host key is persisted.
            self.ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
        self.ssh.connect(args.host, port=port, username=args.user,
                         password=os.environ.get(args.password_env, "vagrant"),
                         look_for_keys=False, allow_agent=False, timeout=15,
                         banner_timeout=15, auth_timeout=15)

    def command(self, command, timeout=30, check=True):
        _, stdout, stderr = self.ssh.exec_command(command, timeout=timeout)
        out, err = [], []
        channel = stdout.channel
        deadline = time.monotonic() + timeout
        while True:
            while channel.recv_ready():
                out.append(channel.recv(65536))
            while channel.recv_stderr_ready():
                err.append(channel.recv_stderr(65536))
            if channel.exit_status_ready() and not channel.recv_ready() and not channel.recv_stderr_ready():
                break
            if time.monotonic() >= deadline:
                channel.close()
                raise TimeoutError(f"{self.role}: remote command timed out")
            time.sleep(0.02)
        result = {"exit_code": channel.recv_exit_status(),
                  "stdout": b"".join(out).decode("utf-8", "replace"),
                  "stderr": b"".join(err).decode("utf-8", "replace")}
        if check and result["exit_code"]:
            raise RuntimeError(f"{self.role}: command failed ({result['exit_code']}): "
                               f"{command}\n{result['stderr']}\n{result['stdout']}")
        return result

    def start(self, args, case, remote_dir):
        source = f"{args.repo}/test/phase3/bin/transfer_{self.role}"
        binary = remote_dir + f"/transfer_{self.role}"
        # Native guest storage avoids transient vboxsf execution failures and
        # insulates a running experiment from subsequent host rebuilds.
        self.command("cp " + shlex.quote(source) + " " + shlex.quote(binary) +
                     " && chmod +x " + shlex.quote(binary))
        argv = [binary, "--bytes", str(args.bytes), "--timeout", str(args.timeout)]
        if self.role == "server":
            argv += ["--read-delay-ms", str(args.read_delay_ms),
                     "--start-delay-ms", str(args.start_delay_ms)]
            if args.recv_capacity is not None:
                argv += ["--recv-capacity", str(args.recv_capacity)]
        command = (f"cd {shlex.quote(args.repo)} && "
                   f"echo $$ > {shlex.quote(remote_dir + '/pid')} && "
                   f"exec env TJU_CC={shlex.quote(case['mode'])} "
                   f"TJU_CHECKSUM={'1' if args.checksum else '0'} "
                   f"TJU_RACK={'1' if args.rack else '0'} "
                   f"TJU_SACK={'1' if args.sack else '0'} "
                   f"TJU_TRACE_DIR={shlex.quote(remote_dir)} " + shlex.join(argv))
        _, stdout, _ = self.ssh.exec_command(command)
        return stdout.channel

    def stop_owned(self, remote_dir, binary):
        # A PID alone can be recycled: verify /proc executable before signalling.
        program = "\n".join([
            "import os, signal, sys, time",
            "pidfile, expected = sys.argv[1:]",
            "try: pid = int(open(pidfile).read().strip())",
            "except (OSError, ValueError): sys.exit(0)",
            "def matches():",
            "    try: return os.path.realpath('/proc/%d/exe' % pid) == os.path.realpath(expected)",
            "    except OSError: return False",
            "for sig in (signal.SIGTERM, signal.SIGKILL):",
            "    if not matches(): break",
            "    try: os.kill(pid, sig)",
            "    except ProcessLookupError: break",
            "    time.sleep(0.15)",
        ])
        return self.command("python3 -c " + shlex.quote(program) + " " +
                            shlex.quote(remote_dir + "/pid") + " " + shlex.quote(binary),
                            check=False)

    def collect(self, remote_dir, local_dir):
        sftp = self.ssh.open_sftp()
        try:
            for entry in sftp.listdir_attr(remote_dir):
                if entry.filename.endswith(".trace"):
                    sftp.get(remote_dir + "/" + entry.filename, str(local_dir / entry.filename))
        finally:
            sftp.close()

    def close(self):
        self.ssh.close()


def network_config(remote, args, config):
    # tcset is installed by this repository's Vagrant box/provisioning.
    argv = ["sudo", "-n", "tcset", args.interface,
            "--rate", f"{config['rate_mbps']:g}Mbps",
            "--delay", f"{config['delay_ms']:g}ms",
            "--loss", f"{config['loss']:g}%", "--overwrite"]
    return remote.command(shlex.join(argv), timeout=45)


def network_state(remote, args):
    return remote.command("tc -s qdisc show dev " + shlex.quote(args.interface), check=False)


def result_from_log(path):
    if not path.exists():
        return None
    for line in reversed(path.read_text(encoding="utf-8", errors="replace").splitlines()):
        if line.startswith("PHASE3_RESULT "):
            return json.loads(line.split(" ", 1)[1])
    return None


def run_case(remotes, args, case):
    stamp = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    run_id = stamp + "_" + case["mode"] + "_" + uuid.uuid4().hex[:8]
    local_dir = args.output.resolve() / run_id
    local_dir.mkdir(parents=True, exist_ok=False)
    remote_dir = "/tmp/tju-phase3-" + run_id
    meta = {"run_id": run_id, "created_utc": stamp, "case": case,
            "bytes_requested": args.bytes, "timeout_seconds": args.timeout,
            "recv_capacity": args.recv_capacity, "read_delay_ms": args.read_delay_ms,
            "start_delay_ms": args.start_delay_ms, "repo": args.repo,
            "remote_artifacts": remote_dir, "ok": False,
            "measurement": "Application goodput; receiver verifies every byte. Close time excluded.",
            "network_note": "delay_ms is the egress delay on each VM (base RTT approximately twice it)",
            "baseline_restore": {"rate_mbps": args.baseline_rate_mbps,
                                 "delay_ms": args.baseline_delay_ms, "loss": args.baseline_loss}}
    channels, files, codes = {}, {}, {}
    network_attempted = []
    cleanup_errors = []
    try:
        for role, remote in remotes.items():
            remote.command("mkdir -p " + shlex.quote(remote_dir))
            state = remote.command("ss -H -lun '( sport = :20218 )'", check=False)
            if state["exit_code"] != 0:
                raise RuntimeError(f"{role}: could not check whether simulation UDP port 20218 is free")
            if state["stdout"].strip():
                raise RuntimeError(f"{role}: UDP port 20218 already in use; refusing to disturb another test")
            meta[role + "_network_before"] = network_state(remote, args)
            meta[role + "_identity"] = remote.command(
                "hostname; uname -a; git -C " + shlex.quote(args.repo) +
                " rev-parse HEAD; git -C " + shlex.quote(args.repo) + " status --short", check=False)
            meta[role + "_source_hashes"] = remote.command(
                "cd " + shlex.quote(args.repo) +
                " && sha256sum src/*.c inc/*.h Makefile test/phase3/transfer_*.c test/phase3/transfer_common.h",
                check=False)
            if case["network"] is not None:
                network_attempted.append(role)
                network_config(remote, args, case["network"])
            meta[role + "_network_during"] = network_state(remote, args)
            files[role] = ((local_dir / (role + ".stdout.log")).open("wb"),
                           (local_dir / (role + ".stderr.log")).open("wb"))

        channels["server"] = remotes["server"].start(args, case, remote_dir)
        # Readiness comes from the listening program, not a guessed fixed delay.
        server_output = bytearray()
        ready_deadline = time.monotonic() + 15
        while b"PHASE3_LISTENING\n" not in server_output:
            channel = channels["server"]
            while channel.recv_ready():
                data = channel.recv(65536)
                server_output += data
                files["server"][0].write(data)
            while channel.recv_stderr_ready():
                files["server"][1].write(channel.recv_stderr(65536))
            if b"PHASE3_LISTENING\n" in server_output:
                break
            if channel.exit_status_ready() or time.monotonic() > ready_deadline:
                raise RuntimeError("server failed to report listening readiness")
            time.sleep(0.02)
        channels["client"] = remotes["client"].start(args, case, remote_dir)
        deadline = time.monotonic() + args.timeout + 15
        while len(codes) < 2:
            for role, channel in channels.items():
                while channel.recv_ready():
                    files[role][0].write(channel.recv(65536))
                while channel.recv_stderr_ready():
                    files[role][1].write(channel.recv_stderr(65536))
                if role not in codes and channel.exit_status_ready():
                    codes[role] = channel.recv_exit_status()
                    if codes[role] != 0:
                        raise RuntimeError(f"{role} exited with {codes[role]}")
            if time.monotonic() >= deadline:
                raise TimeoutError("transfer exceeded host deadline")
            time.sleep(0.02)
        for streams in files.values():
            for stream in streams:
                stream.flush()
        client_result = result_from_log(local_dir / "client.stdout.log")
        server_result = result_from_log(local_dir / "server.stdout.log")
        meta["client_result"], meta["server_result"] = client_result, server_result
        if not client_result or not server_result:
            raise RuntimeError("missing completion JSON from client/server")
        for result in [client_result, server_result]:
            if not result.get("ok") or not result.get("closed") or result["bytes"] != args.bytes:
                raise RuntimeError("incomplete data or connection close")
        if client_result["fnv1a64"] != server_result["fnv1a64"]:
            raise RuntimeError("client/server content hashes differ")
        meta["ok"] = True
    except Exception as exc:
        meta["error"] = f"{type(exc).__name__}: {exc}"
    finally:
        for role, remote in remotes.items():
            try:
                channel = channels.get(role)
                if channel is not None and not channel.exit_status_ready():
                    remote.stop_owned(remote_dir, remote_dir + f"/transfer_{role}")
                if channel is not None:
                    for _ in range(20):
                        while channel.recv_ready():
                            files[role][0].write(channel.recv(65536))
                        while channel.recv_stderr_ready():
                            files[role][1].write(channel.recv_stderr(65536))
                        if channel.exit_status_ready():
                            codes[role] = channel.recv_exit_status()
                            break
                        time.sleep(0.02)
                    channel.close()
                remote.collect(remote_dir, local_dir)
            except Exception as exc:
                cleanup_errors.append(f"{role} collect/cleanup: {exc}")
            if role in network_attempted:
                try:
                    network_config(remote, args, meta["baseline_restore"])
                    meta[role + "_network_restored"] = network_state(remote, args)
                except Exception as exc:
                    cleanup_errors.append(f"{role} NETWORK RESTORE FAILED: {exc}")
        for streams in files.values():
            for stream in streams:
                stream.close()
        meta["exit_codes"] = codes
        if cleanup_errors:
            meta["cleanup_errors"] = cleanup_errors
            meta["ok"] = False
        write_json(local_dir / "metadata.json", meta)
    print(f"{'PASS' if meta['ok'] else 'FAIL'} {local_dir}", flush=True)
    if not meta["ok"]:
        print(meta.get("error", "cleanup failed"), file=sys.stderr)
    return meta


def make_cases(args):
    baseline = {"rate_mbps": args.rate_mbps if args.rate_mbps is not None else args.baseline_rate_mbps,
                "delay_ms": args.delay_ms if args.delay_ms is not None else args.baseline_delay_ms,
                "loss": args.loss if args.loss is not None else args.baseline_loss}
    if args.matrix:
        for mode in args.modes:
            for variable, values in [("loss", args.loss_values), ("delay_ms", args.delay_values)]:
                for value in values:
                    network = dict(baseline)
                    network[variable] = value
                    for repeat in range(1, args.repetitions + 1):
                        yield {"mode": mode, "variable": variable, "value": value,
                               "repeat": repeat, "network": network}
    else:
        changed = any(value is not None for value in [args.loss, args.delay_ms, args.rate_mbps])
        yield {"mode": args.mode, "variable": "single", "repeat": 1,
               "network": baseline if changed else None}


def main():
    args = parse_args()
    def interrupted(signum, frame):
        raise KeyboardInterrupt
    signal.signal(signal.SIGTERM, interrupted)
    local_dependencies = Path(__file__).parent / ".deps"
    if local_dependencies.is_dir():
        sys.path.insert(0, str(local_dependencies))
    try:
        import paramiko
    except ImportError:
        raise SystemExit("Missing dependency: install paramiko in the host Python environment: python -m pip install paramiko")
    remotes = {}
    try:
        # Independent SSH connections in parallel, retaining successes on failure.
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
            futures = {pool.submit(Remote, paramiko, args, role, port): role
                       for role, port in [("client", args.client_port), ("server", args.server_port)]}
            errors = []
            for future in concurrent.futures.as_completed(futures):
                try:
                    remotes[futures[future]] = future.result()
                except Exception as exc:
                    errors.append(str(exc))
            if errors:
                raise RuntimeError("SSH connection failed: " + "; ".join(errors))
        if args.build:
            result = remotes["client"].command(
                "make -C " + shlex.quote(args.repo) + " && make -C " +
                shlex.quote(args.repo + "/test/phase3"), timeout=180)
            print(result["stdout"], end="")
            if result["stderr"]:
                print(result["stderr"], file=sys.stderr, end="")
        results = []
        for case in make_cases(args):
            result = run_case(remotes, args, case)
            results.append({"run_id": result["run_id"], "ok": result["ok"], "case": case})
            if not result["ok"] and not args.continue_on_failure:
                break
        args.output.mkdir(parents=True, exist_ok=True)
        batch_id = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
        write_json(args.output / ("batch_" + batch_id + ".json"), results)
        return 0 if all(result["ok"] for result in results) else 1
    finally:
        for remote in remotes.values():
            remote.close()


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
    except Exception as error:
        print(f"ERROR: {error}", file=sys.stderr)
        sys.exit(1)
