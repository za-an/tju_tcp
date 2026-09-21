#!/usr/bin/env python3
"""Analyze captured phase3 runs, preserving failures and drawing actual data only."""
import argparse
from collections import Counter, defaultdict
import csv
import json
import math
from pathlib import Path
import re
import statistics
import sys

LINE = re.compile(r"^\[(\d+)\]\s+\[([^]]+)\]\s+\[(.*)\]\s*$")
FIELD = re.compile(r"([A-Za-z_][A-Za-z_0-9]*):([^\s\]]+)")


def numeric(value):
    try:
        return int(value)
    except ValueError:
        try:
            return float(value)
        except ValueError:
            return value


def parse_trace(path, bin_seconds=0.25):
    events, reasons = Counter(), Counter()
    points, loss_points = [], []
    stride, point_index = 1, 0
    delivered_bins = defaultdict(int)
    transmitted_bins = defaultdict(int)
    origin = last_time = first_delivery = last_delivery = None
    seen_send_seq = set()
    repeated_packets = repeated_bytes = delivered = sent = malformed = 0
    last_cwnd = last_rwnd = last_ssthresh = last_flight = None
    last_state = None
    zero_window_events = 0
    with path.open(encoding="utf-8", errors="replace") as handle:
        for line in handle:
            match = LINE.match(line)
            if not match:
                malformed += 1
                continue
            timestamp, event, body = match.groups()
            timestamp = int(timestamp) / 1e6
            if origin is None:
                origin = timestamp
            elapsed = timestamp - origin
            last_time = elapsed
            fields = {key: numeric(value) for key, value in FIELD.findall(body)}
            events[event] += 1
            if event == "DELV":
                size = fields.get("size", 0)
                if isinstance(size, (int, float)):
                    delivered += size
                    delivered_bins[int(elapsed / bin_seconds)] += size
                    if first_delivery is None:
                        first_delivery = elapsed
                    last_delivery = elapsed
            if event == "SEND" and fields.get("length", 0) > 0:
                length, seq = fields["length"], fields.get("seq")
                sent += length
                transmitted_bins[int(elapsed / bin_seconds)] += length
                if seq in seen_send_seq:
                    repeated_packets += 1
                    repeated_bytes += length
                seen_send_seq.add(seq)
            window_event = False
            if event == "CC":
                reason = str(fields.get("reason", "unknown"))
                reasons[reason] += 1
                last_cwnd = fields.get("cwnd", last_cwnd)
                last_ssthresh = fields.get("ssthresh", last_ssthresh)
                last_rwnd = fields.get("rwnd", last_rwnd)
                last_flight = fields.get("flight", last_flight)
                last_state = fields.get("state", last_state)
                # These are reactions observed in the stack, not inferred drops.
                if reason.lower() in ("fast", "partial") or any(
                        token in reason.lower() for token in ["rto", "timeout", "fast_retransmit", "fast_enter", "partial_ack"]):
                    loss_points.append({"seconds": elapsed, **fields})
                if last_rwnd == 0:
                    zero_window_events += 1
                window_event = True
            elif event == "CWND":
                last_cwnd = fields.get("size", last_cwnd)
                last_state = fields.get("type", last_state)
                window_event = True
            elif event == "SWND":
                # SWND may be min(cwnd, rwnd); never mislabel it as rwnd.
                pass
            elif event == "RWND":
                # Local receive window; CC.rwnd is the peer receive window.
                if fields.get("size") == 0:
                    zero_window_events += 1
            if window_event:
                if point_index % stride == 0:
                    points.append((elapsed, last_cwnd, last_ssthresh,
                                   last_rwnd, last_flight, last_state))
                point_index += 1
                if len(points) > 40000:
                    points = points[::2]
                    stride *= 2
    duration = (last_delivery - first_delivery
                if last_delivery is not None and first_delivery is not None else None)
    # This trace interval excludes the unknown time before first delivery;
    # application goodput in metadata remains the experiment's main metric.
    summary = {"file": str(path), "event_counts": dict(events), "cc_reasons": dict(reasons),
               "malformed_lines": malformed, "trace_seconds": last_time,
               "delivered_bytes": delivered, "sent_payload_bytes_including_retransmissions": sent,
               "repeated_start_sequence_packets": repeated_packets,
               "repeated_start_sequence_payload_bytes": repeated_bytes,
               "retransmission_estimate_note": "Repeated payload starting sequence numbers; probes and resized retransmissions may differ.",
               "zero_window_events": zero_window_events,
               "first_delivery_seconds": first_delivery, "last_delivery_seconds": last_delivery,
               "delivery_span_goodput_mbps": delivered * 8 / duration / 1e6 if duration and duration > 0 else None,
               "delivery_span_note": "Excludes time before first DELV. DELV means delivered to the TCP receive buffer, not application read.",
               "plot_downsample_stride": stride, "loss_reactions": loss_points}
    return {"summary": summary, "points": points, "delivered_bins": delivered_bins,
            "transmitted_bins": transmitted_bins, "bin_seconds": bin_seconds}


def plot_trace(parsed, output):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    points = parsed["points"]
    fig, axes = plt.subplots(2, 1, figsize=(11, 7), sharex=True, constrained_layout=True)
    labels = [(1, "cwnd", "#0072B2"), (2, "ssthresh", "#D55E00"),
              (3, "peer rwnd", "#009E73"), (4, "FlightSize", "#CC79A7")]
    for column, label, color in labels:
        selected = [(point[0], point[column]) for point in points
                    if isinstance(point[column], (int, float))]
        if selected:
            axes[0].step([p[0] for p in selected], [p[1] for p in selected],
                         where="post", label=label, color=color, linewidth=1.15)
    labelled = set()
    for point in parsed["summary"]["loss_reactions"]:
        reason = point.get("reason", "loss reaction")
        label = reason if reason not in labelled else None
        axes[0].axvline(point["seconds"], color="#666666", linestyle=":",
                        alpha=0.5, linewidth=0.8, label=label)
        labelled.add(reason)
    axes[0].set_ylabel("Bytes")
    axes[0].set_title(Path(parsed["summary"]["file"]).name + " — recorded TCP state")
    if axes[0].get_legend_handles_labels()[0]:
        axes[0].legend(loc="best", ncol=2, fontsize=8)
    width = parsed["bin_seconds"]
    last = parsed["summary"]["trace_seconds"] or 0
    bins = range(int(last / width) + 1)
    for key, label, color in [("delivered_bins", "DELV goodput", "#0072B2"),
                              ("transmitted_bins", "Sent payload incl. retries", "#D55E00")]:
        if parsed[key]:
            axes[1].step([index * width for index in bins],
                         [parsed[key].get(index, 0) * 8 / width / 1e6 for index in bins],
                         where="post", label=label, color=color, linewidth=1)
    axes[1].set_xlabel("Seconds since this trace began (VM-local clock)")
    axes[1].set_ylabel(f"Mbit/s ({width:g} s bins)")
    if axes[1].get_legend_handles_labels()[0]:
        axes[1].legend(loc="best", fontsize=8)
    for axis in axes:
        axis.grid(alpha=0.2)
    fig.savefig(output, dpi=160)
    plt.close(fig)


def write_json(path, data):
    path.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def aggregate_runs(root, plots):
    rows = []
    for path in sorted(root.rglob("metadata.json")):
        try:
            meta = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as exc:
            print(f"Cannot read {path}: {exc}", file=sys.stderr)
            continue
        if "case" not in meta:
            continue
        case = meta["case"]
        network = case.get("network") or {}
        result = meta.get("client_result") or {}
        server = meta.get("server_result") or {}
        rows.append({"run_id": meta.get("run_id", path.parent.name), "ok": bool(meta.get("ok")),
                     "mode": case.get("mode"), "variable": case.get("variable"), "value": case.get("value"),
                     "repeat": case.get("repeat"), "loss_percent": network.get("loss"),
                     "delay_ms_per_vm": network.get("delay_ms"), "rate_mbps": network.get("rate_mbps"),
                     "bytes_requested": meta.get("bytes_requested"),
                     "client_goodput_mbps": result.get("goodput_mbps"),
                     "server_goodput_mbps": server.get("goodput_mbps"),
                     "client_seconds": result.get("seconds"), "error": meta.get("error", "")})
    if not rows:
        raise SystemExit("No captured metadata.json files found; run real experiments first.")
    with (root / "runs.csv").open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    groups = defaultdict(list)
    for row in rows:
        # Include all fixed network settings and size, avoiding incomparable merges.
        key = tuple(row[field] for field in ["mode", "variable", "value", "loss_percent", "delay_ms_per_vm",
                                            "rate_mbps", "bytes_requested"])
        groups[key].append(row)
    summaries = []
    for key, values in groups.items():
        samples = [value["client_goodput_mbps"] for value in values
                   if value["ok"] and isinstance(value["client_goodput_mbps"], (int, float))]
        summaries.append({"mode": key[0], "variable": key[1], "value": key[2],
                          "loss_percent": key[3], "delay_ms_per_vm": key[4], "rate_mbps": key[5],
                          "bytes_requested": key[6], "attempts": len(values), "successful_runs": len(samples),
                          "failed_runs": sum(not value["ok"] for value in values),
                          "mean_goodput_mbps": statistics.mean(samples) if samples else None,
                          "sample_stddev_mbps": statistics.stdev(samples) if len(samples) >= 2 else None,
                          "min_goodput_mbps": min(samples) if samples else None,
                          "max_goodput_mbps": max(samples) if samples else None,
                          "repetition_requirement_met": len(samples) >= 3})
    write_json(root / "summary.json", summaries)
    if plots:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        # Draw one figure per fixed experimental context to avoid mixing sizes/rates.
        contexts = defaultdict(list)
        for row in summaries:
            variable = row["variable"]
            if variable not in ["loss", "delay_ms"] or row["mean_goodput_mbps"] is None:
                continue
            fixed = row["delay_ms_per_vm"] if variable == "loss" else row["loss_percent"]
            contexts[(variable, fixed, row["rate_mbps"], row["bytes_requested"])].append(row)
        for index, (context, data) in enumerate(contexts.items(), 1):
            fig, axis = plt.subplots(figsize=(8, 5), constrained_layout=True)
            for mode in sorted({row["mode"] for row in data}):
                selected = sorted((row for row in data if row["mode"] == mode), key=lambda row: row["value"])
                axis.plot([row["value"] for row in selected], [row["mean_goodput_mbps"] for row in selected],
                          marker="o", label=mode)
                repeated = [row for row in selected if row["sample_stddev_mbps"] is not None]
                if repeated:
                    axis.errorbar([row["value"] for row in repeated],
                                  [row["mean_goodput_mbps"] for row in repeated],
                                  yerr=[row["sample_stddev_mbps"] for row in repeated], fmt="none", capsize=4)
            variable, fixed, rate, byte_count = context
            axis.set_xlabel("Egress loss on each VM (%)" if variable == "loss" else "Egress delay on each VM (ms)")
            axis.set_ylabel("Application goodput (Mbit/s)")
            axis.set_title(f"{byte_count} bytes; rate {rate} Mbit/s; " +
                           (f"delay {fixed} ms/VM" if variable == "loss" else f"loss {fixed}%/VM") +
                           "\nMean of successful runs; error bars = sample SD where n ≥ 2")
            axis.grid(alpha=0.2)
            axis.legend()
            fig.savefig(root / f"comparison_{index}_{variable}.png", dpi=160)
            plt.close(fig)
    return rows, summaries


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", type=Path, help="one run directory, results root, or one .trace file")
    parser.add_argument("--no-plots", action="store_true", help="standard-library-only statistics")
    parser.add_argument("--bin-seconds", type=float, default=0.25)
    args = parser.parse_args()
    if not math.isfinite(args.bin_seconds) or args.bin_seconds <= 0:
        parser.error("--bin-seconds must be finite and positive")
    if not args.no_plots:
        try:
            import matplotlib  # noqa: F401
        except ImportError:
            raise SystemExit("Plotting requires matplotlib: python -m pip install matplotlib (or pass --no-plots)")
    if args.path.is_file():
        traces = [args.path]
    else:
        traces = sorted(args.path.rglob("*.trace"))
        aggregate_runs(args.path, not args.no_plots)
    for path in traces:
        parsed = parse_trace(path, args.bin_seconds)
        output = path.with_suffix(path.suffix + ".summary.json")
        write_json(output, parsed["summary"])
        if not args.no_plots:
            plot_trace(parsed, path.with_suffix(path.suffix + ".png"))
        print(output)
    if not traces:
        print("No trace files found; aggregate uses only captured application results.", file=sys.stderr)


if __name__ == "__main__":
    main()
