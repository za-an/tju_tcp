#encoding: utf-8
import matplotlib.pyplot as plt
import sys
import numpy as np
from pathlib import Path

font_size = 15
OUTPUT_DIR = Path.cwd()
OUTPUT_PREFIX = ''


def figure_path(filename):
    path = OUTPUT_DIR / ('%s%s' % (OUTPUT_PREFIX, filename))
    path.parent.mkdir(parents=True, exist_ok=True)
    return path


def plot_series(time_list, value_list, **kwargs):
    if len(time_list) == 1:
        kwargs.setdefault('marker', 'o')
        kwargs.setdefault('markersize', 5)
    plt.plot(time_list, value_list, **kwargs)

def plot_win(time_list, win_list, win_type, type=[]):
    plt.figure(figsize=(8.0, 4.5))
    plot_series(time_list, win_list, color='#0072B2', drawstyle='steps-post')
    plt.xlabel('Time (s)', fontdict={'size':font_size})
    plt.ylabel('%s Window Size (bytes)'%win_type, fontdict={'size':font_size})

    if win_type=='Congestion':
        map_color = {0: 'red', 1: 'green', 2:'blue', 3:'cyan'}
        map_label = {0: 'slow start', 1: 'congestion avoidance', 2:'fast retransmit', 3:  'timeout'}
        for item in range(4):
            idx = np.argwhere(np.array(type)==item).reshape(1,-1).tolist()[0]
            if len(idx):
                plt.scatter(time_list[idx], win_list[idx], c=map_color[item],
                            s=12, label=map_label[item])
        if len(type):
            plt.legend()

    plt.tight_layout(rect=[0, 0.03, 1, 0.95])
    path = figure_path('%sWindowSize_VS_Time.png' % win_type)
    plt.savefig(path, dpi=300)
    print("绘制成功, 图像位于 %s" % path)
    plt.close()

def plot_all(cwnd_list, rwnd_list, swnd_list):
    plt.figure(figsize=(8.0, 4.5))
    plotted = False
    for values, color, label in (
            (cwnd_list, '#D55E00', 'cwnd'),
            (rwnd_list, '#009E73', 'rwnd'),
            (swnd_list, '#0072B2', 'swnd')):
        if len(values[0]):
            plot_series(values[0], values[1], color=color, label=label,
                        drawstyle='steps-post')
            plotted = True
    if not plotted:
        plt.close()
        print("跳过综合窗口图: Trace 中没有 CWND/RWND/SWND 事件")
        return
    plt.legend(loc=0, numpoints=1)
    plt.xlabel('Time (s)', fontdict={'size':font_size})
    plt.ylabel('Window Size (bytes)', fontdict={'size':font_size})
    plt.tight_layout(rect=[0, 0.03, 1, 0.95])
    path = figure_path('AllWindowSize_VS_Time.png')
    plt.savefig(path, dpi=300)
    print("绘制成功, 图像位于 %s" % path)
    plt.close()

def plot_rtt(time_list, SampleRTT, EstimatedRTT, DeviationRTT, TimeoutInterval):
    plt.figure(figsize=(8.0, 4.5))
    plot_series(time_list, SampleRTT, color='#D55E00', label='SampleRTT')
    plot_series(time_list, EstimatedRTT, color='#009E73', label='EstimatedRTT')
    plot_series(time_list, DeviationRTT, color='#0072B2', label='DeviationRTT')
    plot_series(time_list, TimeoutInterval, color='black', label='TimeoutInterval')
    plt.legend()
    plt.xlabel('Time (s)', fontdict={'size':font_size})
    plt.ylabel('Time (ms)', fontdict={'size':font_size})
    plt.tight_layout(rect=[0, 0.03, 1, 0.95])
    path = figure_path('RTT.png')
    plt.savefig(path, dpi=300)
    print("绘制成功, 图像位于 %s" % path)
    plt.close()

def plot_throughput(time_list, throuput_list, thrp_intv):
    plt.figure(figsize=(8.0, 4.5))
    plot_series(time_list, throuput_list, color='#0072B2')
    plt.xlabel('Time (s)', fontdict={'size':font_size})
    plt.ylabel('throuput (bps)', fontdict={'size':font_size})
    plt.ylim(ymin=0, ymax=max(throuput_list)*1.05)
    plt.tight_layout(rect=[0, 0.03, 1, 0.95])
    path = figure_path('Throughput.png')
    plt.savefig(path, dpi=300)
    print("绘制成功, 图像位于 %s [注: 每%.3fs计算一次瞬时吞吐率]" %
          (path, thrp_intv))
    plt.close()

def read_trace(file):
    SEND_dic = {'utctime':[], 'seq':[], 'ack':[], 'flag':[], 'length':[]}
    RECV_dic = {'utctime':[], 'seq':[], 'ack':[], 'flag':[], 'length':[]}
    CWND_dic = {'utctime':[], 'type':[], 'size':[]}
    RWND_dic = {'utctime':[], 'size':[]}
    SWND_dic = {'utctime':[], 'size':[]}
    RTTS_dic = {'utctime':[], 'SampleRTT':[], 'EstimatedRTT':[], 'DeviationRTT':[], 'TimeoutInterval':[]}
    DELV_dic = {'utctime':[], 'seq':[], 'size':[], 'throughput':[]}

    start_time = 0
    with open(file, 'r', encoding='utf-8') as f:
        for num, line in enumerate(f):
            if(line=='\n'): continue # 跳过空行
            if('SEND' not in line and 'RECV' not in line and 'CWND' not in line and 'RWND' not in line 
            and 'SWND' not in line and 'RTTS' not in line and 'DELV' not in line): continue # 跳过非事件行
            line = line.strip('\n')
            line = line.replace('[', '')
            line = line.replace(']', '')
            line_list = line.split(' ')
            info_list = line_list[2:]
            info_list = [item.split(':')[1] for item in info_list]
            
            if line_list[1] == 'SEND':
                SEND_dic['utctime'].append(int(line_list[0]))
                SEND_dic['seq'].append(int(info_list[0]))
                SEND_dic['ack'].append(int(info_list[1]))
                SEND_dic['flag'].append(info_list[2])
                SEND_dic['length'].append(int(info_list[3]))
            elif line_list[1] == 'RECV':
                RECV_dic['utctime'].append(int(line_list[0]))
                RECV_dic['seq'].append(int(info_list[0]))
                RECV_dic['ack'].append(int(info_list[1]))
                RECV_dic['flag'].append(info_list[2])
                RECV_dic['length'].append(int(info_list[3]))
            elif line_list[1] == 'CWND':
                CWND_dic['utctime'].append(int(line_list[0]))
                CWND_dic['type'].append(int(info_list[0]))
                CWND_dic['size'].append(int(info_list[1]))
            elif line_list[1] == 'RWND':
                RWND_dic['utctime'].append(int(line_list[0]))
                RWND_dic['size'].append(int(info_list[0]))
            elif line_list[1] == 'SWND':
                SWND_dic['utctime'].append(int(line_list[0]))
                SWND_dic['size'].append(int(info_list[0]))
            elif line_list[1] == 'RTTS':
                if line_list[0] not in RTTS_dic['utctime']: 
                    RTTS_dic['utctime'].append(int(line_list[0]))
                    RTTS_dic['SampleRTT'].append(float(info_list[0]))
                    RTTS_dic['EstimatedRTT'].append(float(info_list[1]))
                    RTTS_dic['DeviationRTT'].append(float(info_list[2]))
                    RTTS_dic['TimeoutInterval'].append(float(info_list[3]))
            elif line_list[1] == 'DELV':
                DELV_dic['utctime'].append(int(line_list[0]))
                DELV_dic['seq'].append(int(info_list[0]))
                DELV_dic['size'].append(int(info_list[1])) 

            if start_time==0:
                start_time = int(line_list[0])

    SEND_dic['time'] = [item - start_time for item in SEND_dic['utctime']]
    RECV_dic['time'] = [item - start_time for item in RECV_dic['utctime']]
    CWND_dic['time'] = [item - start_time for item in CWND_dic['utctime']]
    RWND_dic['time'] = [item - start_time for item in RWND_dic['utctime']]
    SWND_dic['time'] = [item - start_time for item in SWND_dic['utctime']]
    RTTS_dic['time'] = [item - start_time for item in RTTS_dic['utctime']]
    DELV_dic['time'] = [item - start_time for item in DELV_dic['utctime']]
    SEND_dic['time'] = np.divide(SEND_dic['time'], 1000000) # 单位: s
    RECV_dic['time'] = np.divide(RECV_dic['time'], 1000000)
    CWND_dic['time'] = np.divide(CWND_dic['time'], 1000000)
    RWND_dic['time'] = np.divide(RWND_dic['time'], 1000000)
    SWND_dic['time'] = np.divide(SWND_dic['time'], 1000000)
    RTTS_dic['time'] = np.divide(RTTS_dic['time'], 1000000)
    DELV_dic['time'] = np.divide(DELV_dic['time'], 1000000)

    return SEND_dic, RECV_dic, CWND_dic, RWND_dic, SWND_dic, RTTS_dic, DELV_dic


FILE_TO_READ = '/vagrant/tju_tcp/test/client.event.trace'
if len(sys.argv)>=2:
	FILE_TO_READ = sys.argv[1]
trace_path = Path(FILE_TO_READ).resolve()
OUTPUT_DIR = trace_path.parent
OUTPUT_PREFIX = trace_path.name.split('.')[0] + '_'
print("正在使用 %s Trace文件绘图"%FILE_TO_READ)
SEND_dic, RECV_dic, CWND_dic, RWND_dic, SWND_dic, RTTS_dic, DELV_dic = read_trace(FILE_TO_READ)

# Keep small traces intact and cap large traces at roughly 3000 points.
event_count = max(len(CWND_dic['utctime']), len(RWND_dic['utctime']),
                  len(SWND_dic['utctime']), len(RTTS_dic['utctime']), 1)
intv = max(1, int(np.ceil(event_count / 3000.0)))
if len(CWND_dic['utctime']):
    plot_win(CWND_dic['time'][::intv], np.array(CWND_dic['size'][::intv]),
             'Congestion', CWND_dic['type'][::intv])
    # plot_win(CWND_dic['time'][::intv], np.array(CWND_dic['size'][::intv]), 'Congestion', CWND_dic['type'][::intv]) # 间隔100个数据进行绘制
    # plot_win(CWND_dic['time'][:100], np.array(CWND_dic['size'][:100]), 'Congestion', CWND_dic['type'][:100]) # 仅绘制前100个数据点
    # plot_win(CWND_dic['time'][100:201], np.array(CWND_dic['size'][100:201]), 'Congestion', CWND_dic['type'][100:201]) # 仅绘制第100到第200的数据点

if len(RWND_dic['utctime']):
    plot_win(RWND_dic['time'][::intv], RWND_dic['size'][::intv], 'Receive')

if len(SWND_dic['utctime']):
    plot_win(SWND_dic['time'][::intv], SWND_dic['size'][::intv], 'Send')

plot_all([CWND_dic['time'][::intv], CWND_dic['size'][::intv]], [RWND_dic['time'][::intv], RWND_dic['size'][::intv]], [SWND_dic['time'][::intv], SWND_dic['size'][::intv]])

if len(RTTS_dic['utctime']): 
    plot_rtt(RTTS_dic['time'], RTTS_dic['SampleRTT'], RTTS_dic['EstimatedRTT'],
             RTTS_dic['DeviationRTT'], RTTS_dic['TimeoutInterval'])

# 每间隔1s绘制一次吞吐率
if len(DELV_dic['utctime']): 
    thrp_intv = 1 # throughput interval
    time_start = DELV_dic['time'][0]
    duration = DELV_dic['time'][-1] - time_start
    intvs = max(1, int(np.ceil(duration / thrp_intv)))
    thrp_list = []
    for i in range(intvs):
        time_end = time_start+thrp_intv
        mark = (DELV_dic['time']>=time_start) & (DELV_dic['time']<time_end)
        payloads_size = np.array(DELV_dic['size'])[mark]
        DELV_dic['throughput'].append(np.sum(payloads_size)*8/thrp_intv) # 单位: bps
        time_start += thrp_intv
    plot_throughput(time_start + np.arange(intvs) * thrp_intv,
                    DELV_dic['throughput'], thrp_intv)
 
