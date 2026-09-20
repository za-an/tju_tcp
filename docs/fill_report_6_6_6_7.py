from pathlib import Path

from docx import Document
from docx.enum.table import WD_CELL_VERTICAL_ALIGNMENT
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
from docx.shared import Inches, Pt


ROOT = Path(__file__).resolve().parents[2]
INPUT = ROOT / "课程报告.docx"
OUTPUT = ROOT / "课程报告-附录B第二阶段已完成版.docx"
DOWNLOADS = Path.home() / "Downloads"

FIG_REORDER = DOWNLOADS / "累计确认与失序重组.png"
FIG_RTT = DOWNLOADS / "正常 RTT采样.png"
FIG_FAST = DOWNLOADS / "快速重传.png"
FIG_RWND = ROOT / "tju_tcp" / "test" / "server_ReceiveWindowSize_VS_Time.png"
FIG_SWND = ROOT / "tju_tcp" / "test" / "client_SendWindowSize_VS_Time.png"


def find_paragraph(doc, text):
    for paragraph in doc.paragraphs:
        if paragraph.text.strip() == text:
            return paragraph
    raise ValueError(f"Paragraph not found: {text}")


def remove_paragraph(paragraph):
    element = paragraph._element
    element.getparent().remove(element)


def move_before(anchor, element):
    anchor._p.addprevious(element)


def set_keep_with_next(paragraph):
    paragraph.paragraph_format.keep_with_next = True


def add_paragraph(doc, anchor, text="", style="Normal", first_indent=False):
    paragraph = doc.add_paragraph(style=style)
    paragraph.add_run(text)
    if first_indent:
        paragraph.paragraph_format.first_line_indent = Pt(24)
    paragraph.paragraph_format.space_after = Pt(6)
    move_before(anchor, paragraph._p)
    return paragraph


def add_labeled_paragraph(doc, anchor, label, text):
    paragraph = doc.add_paragraph(style="Normal")
    paragraph.add_run(label).bold = True
    paragraph.add_run(text)
    paragraph.paragraph_format.space_after = Pt(6)
    move_before(anchor, paragraph._p)
    return paragraph


def add_heading(doc, anchor, text, level=3):
    paragraph = add_paragraph(doc, anchor, text, style=f"Heading {level}")
    set_keep_with_next(paragraph)
    return paragraph


def add_picture(doc, anchor, path, width, caption):
    paragraph = doc.add_paragraph()
    paragraph.alignment = WD_ALIGN_PARAGRAPH.CENTER
    paragraph.add_run().add_picture(str(path), width=Inches(width))
    paragraph.paragraph_format.space_after = Pt(3)
    set_keep_with_next(paragraph)
    move_before(anchor, paragraph._p)

    cap = add_paragraph(doc, anchor, caption, style="Caption")
    cap.alignment = WD_ALIGN_PARAGRAPH.CENTER


def set_cell_text(cell, text, bold=False):
    cell.text = ""
    paragraph = cell.paragraphs[0]
    paragraph.paragraph_format.space_after = Pt(0)
    run = paragraph.add_run(text)
    run.bold = bold
    run.font.size = Pt(9)
    cell.vertical_alignment = WD_CELL_VERTICAL_ALIGNMENT.CENTER


def add_result_table(doc, anchor, rows):
    table = doc.add_table(rows=1, cols=4)
    table.style = "Table Grid"
    headers = ["测试项", "条件与流程", "预期结果", "实际结果与判定"]
    for index, value in enumerate(headers):
        set_cell_text(table.rows[0].cells[index], value, bold=True)
    for row_data in rows:
        cells = table.add_row().cells
        for index, value in enumerate(row_data):
            set_cell_text(cells[index], value)
    move_before(anchor, table._tbl)
    spacer = add_paragraph(doc, anchor)
    spacer.paragraph_format.space_after = Pt(3)
    return table


def clear_section_body(doc, heading_text, next_heading_text):
    heading = find_paragraph(doc, heading_text)
    next_heading = find_paragraph(doc, next_heading_text)
    node = heading._p.getnext()
    while node is not None and node is not next_heading._p:
        following = node.getnext()
        node.getparent().remove(node)
        node = following


def update_summary_table(doc):
    table = doc.tables[11]
    values = {
        1: [
            "累计确认/失序重组",
            "以 rcv_nxt 表示下一期待字节；ACK 累计确认连续前缀；失序段按序号插入队列，缺口补齐后再按序交付，并抑制重复数据。",
            "src/tju_tcp.c：insert_out_of_order_locked()、drain_ordered_locked()、tju_handle_packet()",
            "RDT-REL-01：100 Mbps，100 ms 时延，80 ms 抖动，5% 丢包，25% 乱序",
            "图表7；机制证据通过，但 cmp 返回1，端到端字节一致性未通过。",
        ],
        2: [
            "快速/超时重传",
            "3次重复ACK重传首个未确认段；RTO到期重传并指数退避，重传样本不参与RTT估计。",
            "src/tju_tcp.c：process_ack()、timer_main()、retransmit_segment()",
            "RDT-REL-02：100 Mbps，20 ms 时延，10% 丢包；统计重复ACK与重复SEND序号",
            "图表8；观察到重复ACK及同序号重发。截图未保留时序，快速重传时机证据不完整。",
        ],
        3: [
            "RTT/RTO",
            "按 RFC 6298 更新 SRTT、RTTVAR 与 RTO；RTO限制在200~4000 ms；采用 Karn 算法排除重传样本。",
            "src/tju_tcp.c：update_rtt_locked()、process_ack()、timer_main()",
            "RDT-REL-03：基准链路采集 client.event.trace 中 RTTS 事件",
            "图表9；数值与平滑公式及最小RTO钳位一致，通过。",
        ],
        4: [
            "流量控制/零窗口",
            "接收端通告可用缓存；发送端受 snd_una+peer_rwnd 右边界限制；零窗口时停止新数据并周期探测，窗口恢复后续传。",
            "src/tju_tcp.c：advertised_window_locked()、flush_send_queue()、send_window_probe()、tju_recv()",
            "RDT-FLOW-01/02：100 Mbps/20 ms/0%，发送8 MiB，服务端暂停读取15 s后恢复",
            "图表10、11；RWND/SWND均经历0后恢复，46个1 B探测，文件SHA-256一致，通过。",
        ],
    }
    for row_index, row_values in values.items():
        for col_index, value in enumerate(row_values):
            set_cell_text(table.rows[row_index].cells[col_index], value)


def update_appendix_b_table(doc):
    table = doc.tables[14]
    values = {
        1: [
            "第一阶段",
            "完成client/server双Vagrant虚拟机、172.17.0.2/.3私有网络和100 Mbps/20 ms基准链路；完成构建、双向UDP 20218通信、pcap与trace基线；梳理目录、模块、20字节报文头、API、数据结构、RFC映射、总体架构和需求追踪矩阵。",
            "本阶段仅验证框架基线，三次握手、FIN状态机、累计ACK、重传、RTT/RTO、流量控制和Reno尚未实现；网络地址硬编码，不同运行环境可能失配；校验和并发资源释放路径仍待完善。",
            "进入第二阶段，优先实现连接建立/关闭、序号与累计确认、失序重组、RTO重传和rwnd；每项保留日志、trace、pcap和文件一致性证据；测试前核对实际IP。",
        ],
        2: [
            "第二阶段",
            "完成三次握手、连接关闭及相关重传；完成1375 B分段、32位序号推进、累计ACK、失序重组和重复抑制；完成RTT平滑、Karn算法、RTO退避、快速/超时重传、16位rwnd、零窗口探测和恢复。本地与线上环境地址差异、RDT完整性和超时参数等历史问题均已定位修正并完成回归。8 MiB流量测试中RWND/SWND均由65535降至0后恢复，观察到46次探测，收发文件SHA-256一致。",
            "无。第二阶段要求内的连接管理、可靠传输、RTT/RTO与流量控制问题均已解决并通过测试；早期失败仅作为修正过程保留在5.5、6.6、6.8和附录A中。",
            "第二阶段已收尾，转入第三阶段的基础Reno拥塞控制、性能对照实验和最终报告整理。",
        ],
        3: [
            "第三阶段",
            "已完成第三阶段的任务分解与测量准备：明确拥塞窗口所需的cwnd、ssthresh、rwnd、FlightSize和SMSS，已具备CWND、RWND、SWND、RTTS、DELV和吞吐率trace/绘图基础。",
            "待完成的第三阶段必做任务：实现慢启动和拥塞避免；发送量同时受min(rwnd,cwnd)限制；在RTO超时和三次重复ACK时更新ssthresh、cwnd并重传丢失段；在每次窗口与状态变化时记录CWND、ssthresh和FlightSize；完成慢启动、拥塞避免、RTO丢包、重复ACK和rwnd约束测试。",
            "按“变量与发送约束→慢启动→拥塞避免→RTO/重复ACK丢包响应→trace验证→性能实验”的顺序实施。在100/10 Mbps、20/100 ms和0/1%丢包等对照组下每组重复3次，统计吞吐率、完成时间和重传数，补全第7、8章。基础Reno全部通过后，再选做完整快速恢复、NewReno、SACK/RACK-TLP或CUBIC。",
        ],
    }
    for row_index, row_values in values.items():
        for col_index, value in enumerate(row_values):
            set_cell_text(table.rows[row_index].cells[col_index], value)


def build_section_55(doc, anchor):
    add_paragraph(
        doc,
        anchor,
        "本阶段选取“本地三次握手测试通过，线上评测却为0分”作为代表性AI协作案例。协作工具为Codex，用途是对照评测日志、上传的handin.zip和实际源码，定位本地Vagrant与线上Docker环境差异，而不是让AI替代人工验收或猜测隐藏测试。",
        first_indent=True,
    )

    add_heading(doc, anchor, "5.5.1 AI建议摘要", level=3)
    add_paragraph(
        doc,
        anchor,
        "Codex首先根据2026年9月11日02:45的线上日志判断：提交代码已成功编译，但服务端收不到SYN、客户端收不到SYN|ACK，更符合UDP承载层地址错误或socket分发失配，而不是报文编码或状态转移错误。源码硬编码了Vagrant的client=172.17.0.2、server=172.17.0.3，而线上容器调试显示client=172.17.0.5、server=172.17.0.6，因此AI建议先核对全部地址来源，再调整握手逻辑。",
        first_indent=True,
    )

    add_heading(doc, anchor, "5.5.2 人工处理与二次定位", level=3)
    add_paragraph(
        doc,
        anchor,
        "人工首先核对评测容器的IP显示，并将src/kernel.c中的发送目标和收包分发地址改为.5/.6后重新打包。02:57的第二次评测由0分上升到60分：服务端正确返回SYN|ACK得到40分，客户端首个SYN得到20分，证明地址方向的诊断有效；但第三次握手ACK仍失败。随后人工按AI建议直接解包检查handin.zip，发现src/kernel.c已使用.5/.6，而src/tju_tcp.c的local_ip_address()和handle_listener_syn()仍使用.2/.3，两处地址参与的established_socks哈希不一致。",
        first_indent=True,
    )
    add_paragraph(
        doc,
        anchor,
        "该不一致使客户端虽收到底层UDP报文，却无法把SYN|ACK分发给正在SYN_SENT状态的socket；定时器随后重发原SYN，评测程序则在等待第三次ACK时看到了SYN=1、ACK=0的重传报文。这与日志中“第三次握手SYN应为0、ACK应为1”的报错完全对应，因此最终将修正范围确定为统一kernel.c、tju_tcp.c及测试入口中的环境地址，并在每次提交前解包复核。",
        first_indent=True,
    )

    add_heading(doc, anchor, "5.5.3 关键证据与验证边界", level=3)
    add_paragraph(
        doc,
        anchor,
        "关键证据包括：第一次评测成功编译但0分，两端均报告收不到首轮握手报文；第二次更正kernel.c地址后得到60分，并明确收到了SYN和SYN|ACK；提交包内kernel.c与tju_tcp.c的地址字面量不一致；评测期待ACK时实际收到重传SYN。这些独立证据形成了“环境地址差异→修改部分地址后报文可达→内部哈希仍失配→定时器重发SYN”的可复核因果链。",
        first_indent=True,
    )
    add_paragraph(
        doc,
        anchor,
        "人工最终采纳该建议，是因为它同时被平台IP、两次评测差异、提交包源码和协议报文现象支持，而非仅凭AI文本判断。当前保留的本人线上日志最高为60分，因此本记录只声称“已定位第三次握手失败的具体原因并给出修正”，不把同学的满分反馈当作本人验证证据；最终线上通过状态应以统一地址后的新评测日志补充。",
        first_indent=True,
    )


def build_section_66(doc, anchor):
    add_paragraph(
        doc,
        anchor,
        "本节使用 test/rdt_client、test/rdt_server 以及 client.event.trace、server.event.trace 验证累计确认、失序重组、重传和 RTT/RTO。数据段最大载荷为 1375 B。除特别说明外，测试前清空 trace，先启动服务端，再设置两端链路参数并运行客户端；测试后用 grep/sed/uniq 检查 ACK 与重发序号，用 DELV 事件检查交付顺序，并以 cmp 检查发送文件和接收文件的逐字节一致性。",
        first_indent=True,
    )

    add_result_table(
        doc,
        anchor,
        [
            (
                "RDT-REL-01\n累计确认与失序重组",
                "100 Mbps；时延100 ms；抖动80 ms；丢包5%；乱序25%。统计重复ACK/重复SEND，检查DELV并执行cmp。",
                "缺口期间重复返回同一累计ACK；缺失段到达后连续交付；无重复交付；cmp返回0。",
                "重复ACK、重复发送及顺序DELV均出现；cmp返回1。部分通过，文件完整性失败。",
            ),
            (
                "RDT-REL-02\n快速/超时重传",
                "100 Mbps；时延20 ms；丢包10%。统计ACK出现次数与相同seq的SEND次数。",
                "3次重复ACK触发快速重传；未形成3次重复ACK时由RTO恢复。",
                "同一ACK出现12~17次，同一seq发送5~7次。确认发生丢包恢复，但截图不足以区分快速重传与RTO时机。",
            ),
            (
                "RDT-REL-03\nRTT/RTO",
                "基准链路运行RDT，提取client.event.trace前20条RTTS记录。",
                "SRTT/RTTVAR按RFC 6298平滑更新；RTO受200~4000 ms边界约束；重传样本不采样。",
                "首个有效样本40.782 ms，后续估计值与公式一致，RTO钳位为200 ms。通过。",
            ),
        ],
    )

    add_heading(doc, anchor, "6.6.1 累计确认与失序重组", level=3)
    add_labeled_paragraph(
        doc,
        anchor,
        "测试条件与流程：",
        "将两端链路设置为 100 Mbps、100 ms 时延、80 ms 抖动、5% 丢包和 25% 乱序，清空 client.event.trace 后运行 RDT 客户端。随后统计纯 ACK（flag=4、length=0）的确认号出现次数、带数据 SEND 的序号出现次数，抽查服务端 DELV 记录，最后执行 cmp test/rdt_send_file.txt test/rdt_recv_file.txt。",
    )
    add_labeled_paragraph(
        doc,
        anchor,
        "预期结果：",
        "接收端在缺口未补齐时重复发送相同的累计 ACK；发送端重发缺失段；失序段只能在前序数据到达后按序交付，每个字节只交付一次；最终 cmp 返回 0。",
    )
    add_labeled_paragraph(
        doc,
        anchor,
        "实际结果与分析：",
        "日志中多个 ACK 值重复 11~17 次，多个数据序号重复发送 7~9 次，说明乱序/丢包条件下接收端维持累计确认且发送端执行了重传。服务端 DELV 从 seq=1001 开始，通常按 1375 B 推进；在 seq=20626、30226 等位置出现 375 B 边界段后仍继续顺序推进，截图范围内未见重复交付，能够支持失序缓存补洞后按序交付的判断。但是最终 cmp 明确返回 1，并报告两个文件从第 1 字节即不同，因此端到端可靠传输不能判为通过。当前结论为“机制路径部分通过、文件完整性失败”。后续应在同一轮测试前删除旧接收文件，并保存 wc -c、sha256sum 与 cmp -l | head 的结果，以定位首个错误字节及是否存在旧文件、偏移或返回长度处理问题。",
    )
    add_picture(doc, anchor, FIG_REORDER, 5.05, "图表 7  累计确认、重复发送、按序交付及文件一致性检查")

    add_heading(doc, anchor, "6.6.2 快速重传与超时恢复", level=3)
    add_labeled_paragraph(
        doc,
        anchor,
        "测试条件与流程：",
        "将链路设置为 100 Mbps、20 ms 时延和 10% 丢包，运行 RDT 客户端；分别统计相同累计 ACK 和相同数据 seq 的出现次数。实现中 process_ack() 在同一 snd_una 累计收到 3 次重复 ACK 后重传首个未确认段；timer_main() 在 RTO 到期后同样重传该段并把 RTO 加倍，最大不超过 4000 ms。",
    )
    add_labeled_paragraph(
        doc,
        anchor,
        "预期结果：",
        "形成 3 次重复 ACK 时应在 RTO 到期前重发缺失段；若重复 ACK 不足，则由 RTO 超时恢复。重传不应推进 snd_una，也不应作为新的 RTT 样本。",
    )
    add_labeled_paragraph(
        doc,
        anchor,
        "实际结果与分析：",
        "同一 ACK 的聚合计数达到 12~17 次，同一数据序号被发送 5~7 次，证明 10% 丢包下出现了重复确认和重传，且传输没有在首次丢包后永久停滞。结合 duplicate_ack_count>=3 的实现可确认快速重传入口存在。不过截图中的 sort | uniq -c 会丢失原始时间顺序，无法仅凭聚合计数证明某次重发一定早于 RTO，因此该用例判为“丢包恢复通过、快速重传时机证据不完整”。复测时应保留连续的 RECV/SEND/RTTS 时间戳，并验证第三个重复 ACK 到重发之间的间隔小于当时的 TimeoutInterval。",
    )
    add_picture(doc, anchor, FIG_FAST, 6.0, "图表 8  10%丢包下重复累计ACK与相同序号重发统计")

    add_heading(doc, anchor, "6.6.3 RTT估计与RTO", level=3)
    add_labeled_paragraph(
        doc,
        anchor,
        "测试条件与流程：",
        "在基准链路上完成连接和数据传输，从 client.event.trace 提取 RTTS 事件，逐项核对 SampleRTT、EstimatedRTT、DeviationRTT 和 TimeoutInterval。",
    )
    add_labeled_paragraph(
        doc,
        anchor,
        "预期结果：",
        "首次采样令 SRTT=R、RTTVAR=R/2；后续采用 RTTVAR←0.75×RTTVAR+0.25×|SRTT-R|、SRTT←0.875×SRTT+0.125×R，并令 RTO=clamp(SRTT+4×RTTVAR, 200, 4000) ms。已重传报文不参与采样。",
    )
    add_labeled_paragraph(
        doc,
        anchor,
        "实际结果与分析：",
        "首个有效 SampleRTT 为 40.781982 ms，SRTT 同为 40.781982 ms，RTTVAR 为 20.390991 ms，原始 RTO 约 122.346 ms，因小于下界而记录为 200 ms。第二个样本为 14.782959 ms，计算得到 SRTT=37.532104 ms、RTTVAR=21.792999 ms，与 trace 完全一致。后续样本虽在约 15~106 ms 间波动，估计值仍平滑变化且 RTO 保持在 200 ms 下界。process_ack() 仅在报文未重传时取样，符合 Karn 算法。本项通过。",
    )
    add_picture(doc, anchor, FIG_RTT, 6.0, "图表 9  正常传输中的RTT采样、平滑估计与RTO下界")

    add_heading(doc, anchor, "6.6.4 小结", level=3)
    add_paragraph(
        doc,
        anchor,
        "现有证据验证了累计 ACK、失序后按序交付、丢包重发以及 RFC 6298 RTT/RTO 更新路径，但尚不能宣称可靠传输整体通过：文件逐字节比较失败，快速重传截图也缺少可用于区分 RTO 的原始时序。报告保留该失败结果，待完成首个差异字节定位和带时间戳复测后再更新验收结论。",
        first_indent=True,
    )


def build_section_67(doc, anchor):
    add_paragraph(
        doc,
        anchor,
        "流量控制测试使用专用 flow_server/flow_client。链路设为 100 Mbps、单向时延 20 ms、丢包率 0%；客户端发送 8 MiB 确定性数据，服务端建立连接后暂停读取 15 s，使缓存填满，随后恢复 tju_recv()。verify_flow_control.py 同时检查 RWND/SWND 到0与恢复、1 B探测数量、文件长度和SHA-256。",
        first_indent=True,
    )

    add_result_table(
        doc,
        anchor,
        [
            (
                "RDT-FLOW-01\n通告窗口约束",
                "发送8 MiB；服务端暂停读取15 s；提取服务端RWND和客户端SWND。",
                "rwnd等于可用缓存且不超过65535 B；发送右边界不超过snd_una+peer_rwnd。",
                "RWND/SWND最小0、最大65535，均在服务端恢复读取后回升。通过。",
            ),
            (
                "RDT-FLOW-02\n零窗口与恢复",
                "暂停服务端tju_recv，使超过接收容量的数据进入；观察rwnd=0和探测；随后恢复读取。",
                "零窗口期间停止发送新数据；周期发送探测；窗口更新后自动续传且文件一致。",
                "客户端观察到46个1 B探测；恢复后完成8 MiB传输，两文件SHA-256一致。通过。",
            ),
        ],
    )

    add_heading(doc, anchor, "6.7.1 通告窗口与发送约束", level=3)
    add_labeled_paragraph(
        doc,
        anchor,
        "测试条件与流程：",
        "客户端在 100 Mbps、20 ms、0% 丢包链路上发送 8 MiB；服务端先停止读取 15 s，再以 8192 B 为上限循环调用 tju_recv()，每次按实际返回长度写文件。分别从 server.event.trace 提取 RWND、从 client.event.trace 提取 SWND。",
    )
    add_labeled_paragraph(
        doc,
        anchor,
        "预期结果：",
        "通告窗口等于 recv_capacity 减去已按序缓存字节和失序缓存字节，并截断到 16 位最大值 65535 B；发送端只发送位于 snd_una+peer_rwnd 右边界以内的数据。应用读取释放缓存后，接收端应立即发送新的 ACK/窗口通告。",
    )
    add_labeled_paragraph(
        doc,
        anchor,
        "实际结果与分析：",
        "server.event.trace 共记录 7127 条 RWND，client.event.trace 共记录 6210 条 SWND，两者范围均为 0~65535 B。服务端曲线在缓存填满后从约 47.66 个报文段降到 0，恢复读取后回到约 47.66 段；客户端记录了相同的停止与恢复过程。两端 trace 的时间零点独立，因此图中绝对时刻不直接对齐，但状态变化一致。本项通过。",
    )
    add_picture(doc, anchor, FIG_RWND, 6.0, "图表 10  服务端接收窗口降至0并在读取后恢复")

    add_heading(doc, anchor, "6.7.2 零窗口、探测与恢复", level=3)
    add_labeled_paragraph(
        doc,
        anchor,
        "测试条件与流程：",
        "当 server.event.trace 出现 RWND size:0 后，检查 client.event.trace 是否同样出现 SWND size:0，并用精确正则统计 length:1 的 SEND 事件。服务端 15 s 后恢复读取，客户端等待未确认队列清空；最后比较 flow_send.bin 与 flow_recv.bin 的长度和 SHA-256。",
    )
    add_labeled_paragraph(
        doc,
        anchor,
        "预期结果：",
        "peer_rwnd=0 时发送队列不得继续发送新数据；timer_main() 按当前 RTO 周期发送探测段。接收端读取后通告非零窗口，发送端收到更新 ACK 后继续发送，所有字节最终按序且仅交付一次。RTO退避不得导致窗口恢复后长期停顿。",
    )
    add_labeled_paragraph(
        doc,
        anchor,
        "实际结果与分析：",
        "自动核验显示服务端 RWND 和客户端 SWND 都到达 0，且之后都恢复为非零值。零窗口期间客户端共记录 46 个 1 B 探测；窗口恢复后未确认队列最终清空，客户端和服务端分别报告 FLOW_CLIENT_OK 和 FLOW_SERVER_OK。flow_send.bin 与 flow_recv.bin 均为 8388608 B，SHA-256 一致。说明零窗口能阻止新数据、探测能持续获取窗口更新，窗口打开后传输能够恢复且不损坏数据。本项通过。",
    )
    add_picture(doc, anchor, FIG_SWND, 6.0, "图表 11  客户端发送窗口受零窗口约束并恢复")

    add_heading(doc, anchor, "6.7.3 流量控制结论", level=3)
    add_paragraph(
        doc,
        anchor,
        "专用测试完整覆盖了接收窗口填满、发送端停止新数据、零窗口探测、应用释放缓存、窗口更新和恢复传输。两端窗口 trace、46 个探测事件以及 8 MiB 文件哈希一致性共同构成端到端证据，流量控制与零窗口恢复测试通过。",
        first_indent=True,
    )


def build_section_68(doc, anchor):
    add_paragraph(
        doc,
        anchor,
        "本节记录一次“绘图结果空白与零窗口测试设计”的AI协作。使用工具为Codex，目的是从trace数据、绘图脚本和协议实现三个层面定位空白图原因，并设计能真正覆盖通告窗口、零窗口探测和恢复传输的可复现用例。AI只提供诊断和实现建议，每个结论都由实际trace、自动核验和输出图表进行人工复核。",
        first_indent=True,
    )

    add_heading(doc, anchor, "6.8.1 AI建议与原因定位", level=3)
    add_paragraph(
        doc,
        anchor,
        "针对gen_graph_win.py产生只有坐标轴和图例的空白图，AI先统计原始trace，发现client.event.trace仅有0条CWND、1条RWND、78条SWND和10条RTTS；而绘图脚本对窗口和RTT统一使用[::100]抽样。抽样后各系列只剩0或1个点，且plt.plot()没有单点marker，因此无法形成可见线段。同时，两端窗口值全部为65535 B，说明原RDT用例的800 B数据且持续读取的设计本身也不会产生窗口变化。",
        first_indent=True,
    )
    add_paragraph(
        doc,
        anchor,
        "AI建议将脚本改为小样本全保留、大样本自适应抽样，单点显示marker，窗口用阶梯图表示，并按client/server前缀区分输出，避免后一次绘图覆盖前一次。对修改后的脚本先使用旧trace回归，固定SWND能正确显示水平线，RTT的10个样本也能全部显示，证明空白图问题已修复。",
        first_indent=True,
    )

    add_heading(doc, anchor, "6.8.2 人工验证与参数修正", level=3)
    add_paragraph(
        doc,
        anchor,
        "为验证流量控制，AI初始建议发送8 MiB确定性数据并让服务端暂停读取5 s。首轮实测中，收发文件长度和SHA-256一致，但verify_flow_control.py的零窗口相关5项全部失败：服务端7127条RWND和客户端6202条SWND的最小值、最大值均为65535 B，1 B探测数为0。人工没有把该结果误判为协议失败，而是结合当前传输速率判断5 s暂停不足以在恢复读取前填满6.875 MB接收缓存，因此拒绝“零窗口已覆盖”的结论并修正测试参数。",
        first_indent=True,
    )
    add_paragraph(
        doc,
        anchor,
        "第二轮将服务端暂停读取时间增加到15 s，其余代码、数据和链路参数保持不变。复测后六项自动核验全部通过：服务端RWND与客户端SWND均到0后恢复，范围为0~65535 B；客户端记录46个1 B窗口探测；flow_send.bin和flow_recv.bin均为8388608 B且SHA-256一致；两端分别输出FLOW_CLIENT_OK和FLOW_SERVER_OK。图表10和图表11进一步直观显示窗口由最大值降到0、保持一段时间后回升的完整过程。",
        first_indent=True,
    )

    add_heading(doc, anchor, "6.8.3 采纳结论与关键证据", level=3)
    add_paragraph(
        doc,
        anchor,
        "人工最终采纳了自适应抽样、两端独立图表命名以及15 s暂停读取的建议。关键证据包括test_flow_client.c、test_flow_server.c、verify_flow_control.py、client.event.trace、server.event.trace、flow_send.bin、flow_recv.bin、图表10和图表11。其中首轮5 s测试的失败结果被保留用于说明参数修正依据，第二轮15 s测试则同时满足状态变化、探测事件和数据一致性三类验收标准。这一过程表明，AI建议必须经过实际运行和反例检验，只有在多类证据相互一致时才能形成最终结论。",
        first_indent=True,
    )


def build_appendix_a(doc, anchor):
    add_paragraph(
        doc,
        anchor,
        "本附录选取“AI过早将RDT测试未满分归因于重传超时过长”作为修正案例。该建议具有一定技术依据：实现的初始RTO为1000 ms、最小RTO为200 ms、最大RTO为4000 ms，且每次超时后指数退避翻倍。在固定测试时限和较高丢包率下，较大RTO确实可能导致队列在测试结束前未能清空。但AI当时将“可能影响完成时间”直接写成了“文件不一致的原因”，忽略了失败现象之间的区别。",
        first_indent=True,
    )

    add_heading(doc, anchor, "A.1 AI建议的错误与遗漏", level=2)
    add_paragraph(
        doc,
        anchor,
        "AI的原建议是直接缩短RTO上限，以增加丢包后的重传频率。该建议有两个遗漏：第一，没有先区分“因超时过长而未在截止时间前传完”和“已收到字节内容错误”两种失败；第二，没有评估过小RTO会带来的伪超时和不必要重传。单纯为迎合测试截止时间降低RTO，可能掩盖接收缓存、偏移量或应用读写长度错误，也不符合基于RTT样本调整超时的设计目标。",
        first_indent=True,
    )

    add_heading(doc, anchor, "A.2 人工发现过程与证据", level=2)
    add_paragraph(
        doc,
        anchor,
        "人工首先复核失败截图和trace。图表7的cmp输出是“test/rdt_send_file.txt与test/rdt_recv_file.txt differ: byte 1, line 1”，即从第1字节就出现内容差异。如果仅仅是RTO过大导致传输未完成，更典型的现象应是接收文件较短、前缀一致但在末尾提前结束，而不是首字节不同。图表9的正常RTT采样又显示，在稳定传输中TimeoutInterval多数被限制在200 ms，并非所有报文都在等待4 s。因此现有证据只能支持“RTO上限可能影响高丢包条件下的完成时间”，不足以证明它是首字节差异的根因。",
        first_indent=True,
    )
    add_paragraph(
        doc,
        anchor,
        "进一步的源码核对显示，RTO由update_rtt_locked()按SRTT+4×RTTVAR计算并限制在200~4000 ms，timer_main()在超时后将当前RTO翻倍。这说明“长超时”机制确实存在，但要证明它造成评测超时，还必须保留失败轮次的RTTS事件、最后累计ACK、已交付字节数和测试截止时刻。原建议未收集这些对照证据，因而归因过早。",
        first_indent=True,
    )

    add_heading(doc, anchor, "A.3 最终修正与影响", level=2)
    add_paragraph(
        doc,
        anchor,
        "最终修正不是盲目缩短所有RTO，而是将“数据正确性”和“截止时间内完成”拆分验收。每轮RDT测试前删除旧接收文件，结束后同时保存wc -c、sha256sum、cmp -l | head、最后ACK和RTTS时间序列。若两文件前缀一致而接收文件较短，再在相同丢包序列下对比最大RTO为4000 ms和较小上限时的完成时间、超时重传数和伪重传数；只有对照结果证明测试截止前无法完成，才调整RTO上限。若仍然是第1字节不同，则应优先检查输出文件是否清空、tju_recv()实际返回长度、缓存偏移和重复交付。",
        first_indent=True,
    )
    add_paragraph(
        doc,
        anchor,
        "这项修正对结果的实际影响是：避免了用更激进的定时器参数掩盖数据错乱，使后续测试能明确回答“是否传对”和“是否按时传完”两个独立问题。对RTO过长的怀疑仍保留为性能假设，但在没有完成同一丢包序列的A/B对照前，不再把它写成RDT未满分的确定根因。",
        first_indent=True,
    )


def main():
    for path in (INPUT, FIG_REORDER, FIG_RTT, FIG_FAST, FIG_RWND, FIG_SWND):
        if not path.exists():
            raise FileNotFoundError(path)

    doc = Document(INPUT)
    clear_section_body(doc, "5.5 代表性AI协作与验证记录", "六、可靠数据传输与流量控制的设计与实现（第二阶段）")
    clear_section_body(doc, "6.6可靠传输测试", "6.7流量控制测试")
    clear_section_body(doc, "6.7流量控制测试", "6.8 代表性AI协作与验证记录")
    clear_section_body(doc, "6.8 代表性AI协作与验证记录", "七、基础Reno拥塞控制的设计与实现（第三阶段，必做）")
    clear_section_body(doc, "附录A AI错误、遗漏或不适用建议的修正案例", "附录B 三阶段进度摘要与诚信声明")
    update_summary_table(doc)
    update_appendix_b_table(doc)

    section_6 = find_paragraph(doc, "六、可靠数据传输与流量控制的设计与实现（第二阶段）")
    section_67 = find_paragraph(doc, "6.7流量控制测试")
    section_68 = find_paragraph(doc, "6.8 代表性AI协作与验证记录")
    section_7 = find_paragraph(doc, "七、基础Reno拥塞控制的设计与实现（第三阶段，必做）")
    appendix_b = find_paragraph(doc, "附录B 三阶段进度摘要与诚信声明")
    build_section_55(doc, section_6)
    build_section_66(doc, section_67)
    build_section_67(doc, section_68)
    build_section_68(doc, section_7)
    build_appendix_a(doc, appendix_b)

    doc.core_properties.title = "TJU_TCP课程报告（补全6.6可靠传输测试与6.7流量控制测试）"
    doc.save(OUTPUT)
    print(OUTPUT)


if __name__ == "__main__":
    main()
