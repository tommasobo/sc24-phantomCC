import plotly.express as px
import pandas as pd
from pathlib import Path
import plotly.graph_objs as go
import plotly
from plotly.subplots import make_subplots
from datetime import datetime
import os
import re
import natsort 
from argparse import ArgumentParser

WORKING_DIR = os.path.dirname(os.path.realpath(__file__))
EXP_FOLDER = WORKING_DIR + "/../../" + "sim/output/"

# Parameters
skip_small_value = True
ECN = True

def get_df(path, colnames, max_values=None):
    df = pd.DataFrame(columns=colnames)
    name = ['0'] * df.shape[0]
    df = df.assign(Node=name)

    pathlist = Path(EXP_FOLDER + path).absolute().glob('**/*.txt')
    for file in sorted(pathlist):
        path_in_str = str(file)
        temp_df = pd.read_csv(path_in_str, names=colnames, header=None, index_col=False, sep=',')
        file_name = str(os.path.basename(os.path.normpath(path_in_str)))
        # remove path from name.
        actual_name = file_name[len(path):]
        name = [actual_name] * temp_df.shape[0]
        temp_df = temp_df.assign(Node=name)
        df = pd.concat([df, temp_df])

    if "Time" in df.columns:
        df.drop_duplicates('Time', inplace = True)

    # Change Time to milliseconds for better visualization.
    if "Time" in df.columns:
        df['Time'] = df['Time'].div(1000000)

    if max_values and (len(df) > max_values):
        ratio = len(df) // max_values
        # DownScale
        df = df.iloc[::int(ratio)]
        # Reset the index of the new dataframe
        df.reset_index(drop=True, inplace=True)
    return df

def get_num_columns(path):
    pathlist = Path(EXP_FOLDER + path).absolute().glob('**/*.txt')
    for file in sorted(pathlist):
        temp_df = pd.read_csv(str(file), index_col=False, sep=',')
        return len(temp_df.columns)


def plot_line(fig, df, x_col, y_col, name, color=None, secondary_y=False):
    color = color if color else None
    fig.add_trace(
        go.Scatter(x=df[x_col], y=df[y_col], mode='markers', marker=dict(size=2), name=name, line=dict(color=color), opacity=0.9, showlegend=True),
        secondary_y=secondary_y,
    )

def main(args):
    df = get_df('rtt', ['Time', 'RTT', 'seqno', 'ackno', 'base', 'target'], max_values=10000)
    # Change the RTT to microseconds.
    df['RTT'] = df['RTT'].div(1000000)

    base_rtt = df["base"].max()
    target_rtt = df["target"].max()

    # Cwd data.
    df2 = get_df('cwd', ['Time', 'Congestion Window'], max_values=10000)

    # Change the Congestion Window to megabytes from bytes.
    df2['Congestion Window'] = df2['Congestion Window'].div(1000000)


    df30 = get_df('queue_phantom', ['Time', 'Queue', 'KMin', 'KMax'], max_values=10000)


    if get_num_columns('queue') == 4:
        df3 = get_df('queue', ['Time', 'Queue', 'KMin', 'KMax'], max_values=10000)
    else:
        df3 = get_df('queue', ['Time', 'Queue', 'KMin', 'KMax', 'Max'], max_values=10000)


    df5 = get_df('sent', ['Time', 'Sent'], max_values=10000)

    df4 = get_df('ecn', ['Time', 'ECN'], max_values=10000)

    # Nack data
    df6 = get_df('nack', ['Time', 'Nack'], max_values=10000)

    # Unacked data.
    # df_unacked = get_df('unacked', ['Time', 'Unacked'], max_values=10000)

    # Acked Bytes Data
    df8 = get_df('acked', ['Time', 'AckedBytes'], max_values=10000)

    # ECN in RTT Data
    df13 = get_df('ecn_rtt', ['Time', 'ECNRTT'], max_values=10000)

    # Trimming in RTT Data
    df14 = get_df('trimmed_rtt', ['Time', 'TrimmedRTT'], max_values=10000)

    # FastI data
    df9 = get_df('fasti', ['Time', 'FastI'], max_values=10000)

    # FastD data
    df10 = get_df('fastd', ['Time', 'FastD'], max_values=10000)

    # MediumI data
    df11 = get_df('mediumi', ['Time', 'MediumI'], max_values=10000)


    # Target RTT low data
    df_target_rtt_low = get_df('target_rtt_low', ['Time', 'target_rtt_low'], max_values=10000)
    # change the target rtt low to microseconds.
    df_target_rtt_low['target_rtt_low'] = df_target_rtt_low['target_rtt_low'].div(1000000)

    # Target RTT high data
    df_target_rtt_high = get_df('target_rtt_high', ['Time', 'target_rtt_high'], max_values=10000)
    # change the target rtt high to microseconds.
    df_target_rtt_high['target_rtt_high'] = df_target_rtt_high['target_rtt_high'].div(1000000)

    # Baremetal RTT data.
    df_baremetal_latency = get_df('baremetal_latency', ['Time', 'BaremetalLatency'], max_values=10000)
    # change the baremetal rtt to microseconds.
    df_baremetal_latency['BaremetalLatency'] = df_baremetal_latency['BaremetalLatency'].div(1000000)

    # Current RTT EWMA
    df_current_rtt_ewma = get_df('current_rtt_ewma', ['Time', 'current_rtt_ewma'])
    # change the current rtt ewma to microseconds.
    df_current_rtt_ewma['current_rtt_ewma'] = df_current_rtt_ewma['current_rtt_ewma'].div(1000000)

    # Ecn EWMA.
    df_ecn_ewma = get_df('ecn_ewma', ['Time', 'ecn_ewma'], max_values=10000)

    df_rtt_congested = get_df('rtt_congested', ['Time'], max_values=10000)
    df_ecn_congested = get_df('ecn_congested', ['Time'], max_values=10000)

    df_qa_free = get_df('qa_free', ['Time'], max_values=10000)

    # Parameters.
    param_df = get_df("params", ['Parameter', 'Value'])

    print(param_df)
    bdp = param_df.loc[param_df['Parameter'] == 'BDP (KB)']['Value'].values[0] / 1000

    # For every node print the average and 99 percentile RTT.
    summary_stats_df = pd.DataFrame(columns=['Node', 'Mean RTT (us)', '99 Percentile RTT (us)', 'Avg Drop Rate %', 'FCT (us)', 'Ideal FCT (us)', 'Avg Queue Latency (us)']) 
    # Find highest average queue length.
    max_queue_latency = df3['Queue'].max()
    for i in df['Node'].unique():
        sub_df = df.loc[df['Node'] == str(i)]
        mean_rtt = sub_df['RTT'].mean()
        percentile_99_rtt = sub_df['RTT'].quantile(0.99)
        nack_df = df6.loc[df6['Node'] == str(i)]
        acked_df = df8.loc[df8['Node'] == str(i)]
        drop_rate = (nack_df['Nack'].sum() / (nack_df['Nack'].sum() + (acked_df['AckedBytes'].max() / 4096))) * 100
        # unacked_sub_df = df_unacked.loc[df_unacked['Node'] == str(i)]
        sent_df = df5.loc[df5['Node'] == str(i)]
        fct = sub_df['Time'].max() - sent_df['Time'].min()
        ideal_fct_us = args.ideal_fct_us + base_rtt/1000 if args.ideal_fct_us is not None else None
        summary_stats_df.loc[i] = [i, mean_rtt / 1000, percentile_99_rtt / 1000, drop_rate, fct / 1000, ideal_fct_us, max_queue_latency / 1000]
    summary_stats_df = summary_stats_df.round(decimals=2)

    pd.set_option('display.float_format', '{:.3f}'.format)
    print(summary_stats_df.to_string(index=False))
    print("Max Queue Length (us): ", max_queue_latency / 1000)

    print("Finished Parsing")
    
    fig = make_subplots(rows=3, cols=1, specs=[[{}], [{}], [{}]], shared_xaxes=True, vertical_spacing=0.1, subplot_titles=("Congestion Window", "RTT", "Queue Length"))
    color = ['#636EFA', '#0511a9', '#EF553B', '#00CC96', '#AB63FA', '#FFA15A', '#19D3F3', '#FF6692', '#B6E880', '#FF97FF', '#FECB52']

    colors = ['purple', 'orange', 'black', '#66FF00', '#636EFA', '#0511a9', '#EF553B', '#00CC96', '#AB63FA', '#FFA15A', '#19D3F3', '#FF6692', '#B6E880', '#FF97FF', '#FECB52']
    line_types = ['dash', 'dot']

    # Add traces
    mean_rtt = df["RTT"].mean()
    max_rtt = df["RTT"].max()
    max_cwd = df2["Congestion Window"].max()
    max_x = df["Time"].max()
    count = 0

    flow_name_to_num = {}
    for i in df2['Node'].unique():
        flow_name = str(i).replace(".txt", "")
        flow_name_to_num[flow_name] = "Flow-"+str(len(flow_name_to_num) + 1)

    if not args.cor:
        max_cwd = 0
        for index, i in enumerate(df2['Node'].unique()):
            if args.intra_only:
                if "lcp" in i:
                    continue
            sub_df = df2.loc[df2['Node'] == str(i)]
            flow_name = str(i).replace(".txt", "")
            fig.add_trace(
                go.Scatter(x=sub_df["Time"], y=sub_df['Congestion Window'], name="CWD-"+str(flow_name_to_num[flow_name]).replace("cwd/cwdEqds_", ""), line=dict(dash=line_types[index%len(line_types)], color=colors[index%len(colors)]), showlegend=True, legendgroup=2),
                row=1, col=1,
            )
            max_cwd = max(max_cwd, max(sub_df['Congestion Window']))
            # Change subplot title.
        fig.update_yaxes(title_text="CWND (MB)", row=1, col=1)
        y_nack = max_cwd * 0.80
        y_ecn = max_cwd * 0.85
        y_fasti = max_cwd * 0.75
        y_fastd = max_cwd * 0.70
        y_ecn_congested = max_cwd * 0.90
        y_rtt_congested = max_cwd * 0.95
        y_qa_free = max_cwd * 0.98
    else:
        max_rate = 0

        # intra_rtt_us = 14.027520
        # # inter_rtt_us = 20019.041280
        # inter_rtt_us = 2000
        for index, i in enumerate(df2['Node'].unique()):
            # if "mprdma" in i:
            #     continue
            sub_df = df2.loc[df2['Node'] == str(i)]
            # if "mprdma" in i:
            #     sub_df['Rate (Gbps)'] = 8 * 1000 * sub_df['Congestion Window'] / intra_rtt_us
            # else:
            #     sub_df['Rate (Gbps)'] = 8 * 1000 * sub_df['Congestion Window'] / inter_rtt_us

            sub_rtt_df = df[df['Node'] == str(i)]
            # Sort both dataframes by time.
            sub_df = sub_df.sort_values(by=['Time'])
            sub_rtt_df = sub_rtt_df.sort_values(by=['Time'])

            # Turn into list.
            sub_df = sub_df.values.tolist()
            sub_rtt_df = sub_rtt_df.values.tolist()

            times = []
            cwnd_over_rtt = []

            cwd_index = 0
            rtt_index = 0

            # print(f"Length of sub_df: {len(sub_df)}")
            # print(f"Length of sub_rtt_df: {len(sub_rtt_df)}")

            def rate(cwnd, rtt):
                return 8 * cwnd / rtt

            while cwd_index < len(sub_df) or rtt_index < len(sub_rtt_df):
                # print(f"cwd_index: {cwd_index}, rtt_index: {rtt_index}")
                if cwd_index == len(sub_df) - 1:
                    # Add the last value of the cwnd.
                    times.append(sub_df[cwd_index][0])
                    cwnd_over_rtt.append(rate(sub_df[cwd_index][1], sub_rtt_df[rtt_index][1]))
                    break
                elif rtt_index == len(sub_rtt_df) - 1:
                    # Add the last value of the rtt.
                    times.append(sub_rtt_df[rtt_index][0])
                    cwnd_over_rtt.append(rate(sub_df[cwd_index][1], sub_rtt_df[rtt_index][1]))
                    cwd_index += 1
                elif sub_df[cwd_index+1][0] < sub_rtt_df[rtt_index+1][0]:
                    cwd_index += 1
                    times.append(sub_df[cwd_index][0])
                    cwnd_over_rtt.append(rate(sub_df[cwd_index][1], sub_rtt_df[rtt_index][1]))
                else:
                    rtt_index += 1

            max_rate = max(max_rate, max(cwnd_over_rtt))
            
            flow_name = str(i).replace(".txt", "")
            # fig.add_trace(
            #     go.Scatter(x=sub_df["Time"], y=sub_df['Congestion Window'], name="CWD-"+str(flow_name_to_num[flow_name]).replace("cwd/cwdEqds_", ""), line=dict(dash=line_types[index%len(line_types)], color=colors[index%len(colors)]), showlegend=False, legendgroup=3),
            #     row=1, col=1,
            # )

            fig.add_trace(
                go.Scatter(x=times, y=cwnd_over_rtt, name="Rate-"+str(flow_name_to_num[flow_name]), line=dict(dash=line_types[index%len(line_types)], color=colors[index%len(colors)]), showlegend=True, legendgroup=2),
                secondary_y=False,
                row=1, col=1,
            )

            # fig.add_trace(
            #     go.Scatter(x=sub_df["Time"], y=sub_df['Rate (Gbps)'], name="Rate-"+str(flow_name_to_num[flow_name]), line=dict(dash=line_types[index%len(line_types)], color=colors[index%len(colors)]), showlegend=True, legendgroup=2),
            #     secondary_y=False,
            #     row=1, col=1,
            # )

        y_nack = max_rate * 0.80
        y_ecn = max_rate * 0.85
        y_fasti = max_rate * 0.75
        y_fastd = max_rate * 0.70
        y_ecn_congested = max_rate * 0.90
        y_rtt_congested = max_rate * 0.95
        y_qa_free = max_rate * 0.98

        fig.update_yaxes(title_text="Rate (Gbps)", row=1, col=1)

    count = 0
    df6['Nack'] = df6['Nack'].multiply(y_nack)
    for i in df6['Node'].unique():
        sub_df6 = df6.loc[df6['Node'] == str(i)]
        fig.add_trace(
            go.Scatter(x=sub_df6["Time"], y=sub_df6["Nack"], mode="markers", marker_symbol="triangle-up", name="NACK Packet", marker=dict(size=5, color="grey"), showlegend=True if count == 0 else False, legendgroup=2),
            secondary_y=False,
            row=1, col=1,
        )
        count += 1

    count = 0
    df9['FastI'] = df9['FastI'].multiply(y_fasti)
    for i in df9['Node'].unique():
        sub_df9 = df9.loc[df9['Node'] == str(i)]
        fig.add_trace(
            go.Scatter(x=sub_df9["Time"], y=sub_df9["FastI"], mode="markers", marker_symbol="triangle-up", name="FastI Packet", marker=dict(size=5, color="brown"), showlegend=True if count == 0 else False, legendgroup=2),
            secondary_y=False,
            row=1, col=1,
        )
        count += 1

    print("FastD Plot")
    # FastD
    count = 0
    df10['FastD'] = df10['FastD'].multiply(y_fastd)
    for i in df10['Node'].unique():
        sub_df10 = df10.loc[df10['Node'] == str(i)]
        fig.add_trace(
            go.Scatter(x=sub_df10["Time"], y=sub_df10["FastD"], mode="markers", marker_symbol="triangle-up", name="FastD Packet", marker=dict(size=5, color="black"), showlegend=True if count == 0 else False, legendgroup=2),
            secondary_y=False,
            row=1, col=1,
        )
        count += 1

    # ECN
    count = 0
    df4['ECN'] = df4['ECN'].multiply(y_ecn)
    for i in df4['Node'].unique():
        sub_df4 = df4.loc[df4['Node'] == str(i)]
        fig.add_trace(
            go.Scatter(x=sub_df4["Time"], y=sub_df4['ECN'], mode="markers", marker_symbol="triangle-up", name="ECN Packet", marker=dict(size=5, color="yellow"), showlegend=True if count == 0 else False, legendgroup=2),
            secondary_y=False,
            row=1, col=1,
        )
        count += 1

    # ECN congested.
    count = 0
    for i in df_ecn_congested['Node'].unique():
        sub_df = df_ecn_congested.loc[df_ecn_congested['Node'] == str(i)]
        fig.add_trace(
            go.Scatter(x=sub_df["Time"], y=[y_ecn_congested] * len(sub_df), name="ECN Congested", mode="markers", marker_symbol="triangle-up",  marker=dict(size=5, color="purple"), legendgroup=2, showlegend=True if count == 0 else False),
            secondary_y=False,
            row=1, col=1,
        )
        count += 1

    # QA free.
    count = 0
    for i in df_qa_free['Node'].unique():
        sub_df = df_qa_free.loc[df_qa_free['Node'] == str(i)]
        fig.add_trace(
            go.Scatter(x=sub_df["Time"], y=[y_qa_free] * len(sub_df), name="QA Free", mode="markers", marker_symbol="triangle-up",  marker=dict(size=5, color="blue"), legendgroup=2, showlegend=True if count == 0 else False),
            secondary_y=False,
            row=1, col=1,
        )
        count += 1
    
    # RTT congested.
    count = 0
    for i in df_rtt_congested['Node'].unique():
        sub_df = df_rtt_congested.loc[df_rtt_congested['Node'] == str(i)]
        fig.add_trace(
            go.Scatter(x=sub_df["Time"], y=[y_rtt_congested] * len(sub_df), name="RTT Congested", mode="markers", marker_symbol="triangle-up",  marker=dict(size=5, color="red"), legendgroup=2, showlegend=True if count == 0 else False),
            secondary_y=False,
            row=1, col=1,
        )
        count += 1

    #     '''
    #     fig.add_trace(
    #         go.Scatter(x=sub_df["Time"], y=sub_df['RTT'], mode='markers', marker=dict(size=10), name=str(i), line=dict(color=color[0]), opacity=0.9, showlegend=True, marker_symbol="triangle-up"),
    #         secondary_y=False,
    #     )
    #     '''
    #     if (args.show_triangles is not None):
    #         fig.add_trace(
    #             go.Scatter(x=sub_df["Time"], y=sub_df['RTT'], mode="markers", marker_symbol="triangle-up", name="Mark Packet", marker=dict(size=6, color=color[1]), showlegend=True),
    #             secondary_y=False,
    #             row=1, col=1,
    #         )
    #     if (args.num_to_show == 1):
    #         break

    # if not args.intra_only:
    #     # Change the RTT to microseconds.
    #     df['RTT'] = df['RTT'].div(1000)
    for i in df['Node'].unique():
        sub_df = df.loc[df['Node'] == str(i)]
        if args.intra_only:
            if "lcp" in str(i):
                continue
        else:
            if "mprdma" in str(i):
                continue
        fig.add_trace(
            go.Scatter(x=sub_df["Time"], y=sub_df['RTT'], mode='markers', marker=dict(size=2), name="RTT-"+str(i), line=dict(), opacity=0.9, showlegend=False, legendgroup=1),
            secondary_y=False,
            row=2, col=1
        )

    if not args.intra_only:
        # Target RTT high.
        # df_target_rtt_high['target_rtt_high'] = df_target_rtt_high['target_rtt_high'].multiply(y_target_rtt_high)
        fig.add_trace(
            go.Scatter(x=df_target_rtt_high["Time"], y=df_target_rtt_high['target_rtt_high'], name="Target RTT High", line=dict(dash='solid', color='red'), showlegend=True, legendgroup=1),
            secondary_y=False,
            row=2, col=1,
        )

        # Target RTT low.
        # df_target_rtt_low['target_rtt_low'] = df_target_rtt_low['target_rtt_low'].multiply(y_target_rtt_low)
        fig.add_trace(
            go.Scatter(x=df_target_rtt_low["Time"], y=df_target_rtt_low['target_rtt_low'], name="Target RTT Low", line=dict(dash='solid', color='blue'), showlegend=True, legendgroup=1),
            secondary_y=False,
            row=2, col=1,
        )

        # Baremetal RTT.
        # df_baremetal_latency['BaremetalLatency'] = df_baremetal_latency['BaremetalLatency'].multiply(y_baremetal_latency)
        fig.add_trace(
            go.Scatter(x=df_baremetal_latency["Time"], y=df_baremetal_latency['BaremetalLatency'], name="Baremetal RTT", line=dict(dash='solid', color='green'), showlegend=True, legendgroup=1),
            secondary_y=False,
            row=2, col=1,
        )
    
    # for index, i in enumerate(df_current_rtt_ewma['Node'].unique()):
    #     sub_df = df_current_rtt_ewma.loc[df_current_rtt_ewma['Node'] == str(i)]
    #     flow_name = str(i).replace(".txt", "")
    #     fig.add_trace(
    #         go.Scatter(x=sub_df["Time"], y=sub_df['current_rtt_ewma'], name="Latency-"+str(flow_name_to_num[flow_name]), line=dict(dash=line_types[index%len(line_types)], color=colors[index%len(colors)]), showlegend=True, legendgroup=1),
    #         secondary_y=False,
    #         row=1, col=1,
    #     )

    # # Plot BDP.
    # fig.add_trace(
    #     go.Scatter(x=df["Time"], y=[bdp] * len(df), name="BDP", line=dict(dash='dash', color='red'), showlegend=True, legendgroup=2),
    #     secondary_y=False,
    #     row=1, col=1,
    # )

    # # Summed CWD stores the sum of all the CWDs. For each flow, the CWND at a given time is the last value of the flow before the measured time.
    # summed_cwd = pd.DataFrame(columns=['Time', 'Summed Congestion Window'])
    # # go through each flow in time order.
    # flow_to_cwd = {}
    # for i in df2['Node'].unique():
    #     flow_name = str(i).replace(".txt", "")
    #     flow_to_cwd[flow_name] = 0
    # # Sort df2 by time.
    # df2 = df2.sort_values(by=['Time'])
    # time_to_summed_cwd = []

    # #Turn df2 into a list.
    # df2_list = df2.values.tolist()
    # for i in range(1, len(df2_list)):
    #     row = df2_list[i]
    #     time = row[0]
    #     flow_name = str(row[2]).replace(".txt", "")
    #     flow_to_cwd[flow_name] = float(row[1])
    #     time_to_summed_cwd.append([time, sum(flow_to_cwd.values())])

    # summed_cwd = pd.DataFrame(time_to_summed_cwd, columns=['Time', 'Summed Congestion Window'])

    # for i in df2.iterrows():
    #     row = i[1]
    #     time = row['Time']
    #     flow_name = str(row['Node']).replace(".txt", "")
    #     flow_to_cwd[flow_name] = row['Congestion Window']
    #     summed_cwd.loc[len(summed_cwd)] = [time, sum(flow_to_cwd.values())]
    # Plot the summed CWD.
    # fig.add_trace(
    #     go.Scatter(x=summed_cwd["Time"], y=summed_cwd['Summed Congestion Window'], name="Summed CWD", line=dict(dash='solid', color='black'), showlegend=True, legendgroup=2),
    #     secondary_y=False,
    #     row=1, col=1,
    # )



    # for index, i in enumerate(df_ecn_ewma['Node'].unique()):
    #     sub_df = df_ecn_ewma.loc[df_ecn_ewma['Node'] == str(i)]
    #     fig.add_trace(
    #         go.Scatter(x=sub_df["Time"], y=sub_df['ecn_ewma'], name="ECN EWMA", line=dict(dash=line_types[index%len(line_types)], color=colors[index%len(colors)]), showlegend=True, legendgroup=4),
    #         secondary_y=False,
    #         row=4, col=1,
    #     )

    # # Plot Queue lengths.
    # for index, i in enumerate(df3['Node'].unique()):
    #     sub_df = df3.loc[df3['Node'] == str(i)]
    #     flow_name = str(i).replace(".txt", "")
    #     fig.add_trace(
    #         go.Scatter(x=sub_df["Time"], y=sub_df['Queue'], name="Queue-"+str(flow_name_to_num[flow_name]), line=dict(dash=line_types[index%len(line_types)], color=colors[index%len(colors)]), showlegend=True, legendgroup=4),
    #         secondary_y=False,
    #         row=4, col=1,
    #     )
    
    # Queue
    print("Queue Plot")
    count = 0
    df3['Queue'] = pd.to_numeric(df3['Queue'])
    max_ele = df3[['Queue']].idxmax(1)
    for i in df3['Node'].unique():
        sub_df = df3.loc[df3['Node'] == str(i)]
        # if (skip_small_value is True and sub_df['Queue'].max() < 500):
        #     count += 1
        #     continue

        fig.add_trace(
            go.Scatter(x=sub_df["Time"], y=sub_df['Queue'], name="Queue " + str(i),   mode="markers",  marker=dict(size=1.4), line=dict(dash='dash', color=colors[count%len(colors)], width=3),  showlegend=True, legendgroup=3),
            secondary_y=False,
            row=3, col=1,
        )
        count += 1

    if args.name is not None:
        my_title=args.name
    else:
        my_title="<b>Incast 2:1 – 1:1 FT – 800Gbps – 4KiB MTU – 128MiB Flows - LoadBalancing ON</b>"

    # Add figure title
    fig.update_layout(title_text=my_title)

    print("Done Plotting")

    # Set x-axis title
    fig.update_xaxes(title_text="Time (ms)")
    
    # Set the y axis title for the first plot.
    fig.update_yaxes(title_text="RTT (us)", row=2, col=1)
    fig.update_yaxes(title_text="Latency (ns)", row=3, col=1)
    fig.update_xaxes(title_text="", row=1, col=1)
    fig.update_xaxes(title_text="", row=1, col=1)

    # Update row 4 y axes to go from 0 to 1.
    # fig.update_yaxes(range=[0, 1], row=4, col=1)

    if args.log_scale:
        fig.update_yaxes(type="log", row=1, col=1)

    # Set font size for axes and legend.
    fig.update_layout(
        font=dict(
            family="Courier New, monospace",
            size=20
        ), legend_tracegroupgap=400
    )

    
    now = datetime.now() # current date and time
    date_time = now.strftime("%m:%d:%Y_%H:%M:%S")
    #fig.write_image("out/fid_simple_{}.png".format(date_time))
    #plotly.offline.plot(fig, filename='out/fid_simple_{}.html'.format(date_time))
    if (args.output_folder is not None):
        # Don't display.
        plotly.offline.plot(fig, filename=args.output_folder + "/{}.html".format(args.name), auto_open=False)
        fig.write_image(args.output_folder + "/{}.png".format(args.name), height=800, width=1600, scale=1.0)

        # If command file exists read it and save it.
        if os.path.exists("sim/output/cmd/cmd.txt".format(args.name)):
            with open("sim/output/cmd/cmd.txt".format(args.name), 'r') as file:
                data = file.read().replace('\n', '')
                with open(args.output_folder + "/"+(args.name)+"-cmd.txt", 'w') as f:
                    f.write(data)
    elif (args.no_show is None):
        if (args.output_folder is not None):
            plotly.offline.plot(fig, filename=args.output_folder + "/{}.html".format(args.name))
            # # Save to png as well with a particular size.
            # fig.write_image(args.output_folder + "/{}.png".format(args.name), height=550, width=1000, scale=4)
        else:
            fig.show()

if __name__ == "__main__":
    parser = ArgumentParser()
    parser.add_argument("--x_limit", type=int, help="Max X Value showed", default=None)
    parser.add_argument("--y_limit", type=int, help="Max Y value showed", default=None)
    parser.add_argument("--show_ecn", type=str, help="Show ECN points", default=None)
    parser.add_argument("--show_sent", type=str, help="Show Sent Points", default=None) 
    parser.add_argument("--show_triangles", type=str, help="Show RTT triangles", default=None) 
    parser.add_argument("--num_to_show", type=int, help="Number of lines to show", default=None) 
    parser.add_argument("--annotations", type=str, help="Number of lines to show", default=None) 
    parser.add_argument("--output_folder", type=str, help="OutFold", default=None) 
    parser.add_argument("--input_folder", type=str, help="InFold", default=None) 
    parser.add_argument("--name", type=str, help="Name Algo", default=None) 
    parser.add_argument("--no_show", type=int, help="Don't show plot, just save", default=None) 
    parser.add_argument("--show_case", type=int, help="ShowCases", default=None) 
    parser.add_argument("--cumulative_case", type=int, help="Do it cumulative", default=None) 
    parser.add_argument("--ideal_fct_us", type=int, help="Ideal FCT", default=None)
    parser.add_argument("--log_scale", action='store_true', help="Log Scale", default=False)
    parser.add_argument("--cor", action='store_true', help="CWND over RTT", default=False)
    parser.add_argument("--intra_only", action='store_true', help="Intra only", default=False)
    args = parser.parse_args()

    if args.input_folder is not None:
        EXP_FOLDER = WORKING_DIR + "/../../" + args.input_folder + "/"
        print(EXP_FOLDER)
    main(args)
