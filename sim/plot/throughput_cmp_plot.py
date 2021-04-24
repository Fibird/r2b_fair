import matplotlib.pyplot as plt
from datetime import datetime
import numpy as np
import math

color_styles = ['#d73027', '#f46d43', '#2c7bb6', '#fdae61', '#fee090', '#ffffbf', '#e0f3f8', '#abd9e9', '#74add1', '#4575b4']
markers = ['x', 'o', '>', 'square', '*', '<']
linestyles = ['solid', 'dashed', 'dashdot', 'dotted']
client_labels = ['B, burst=3000', 'R, rserve=700', 'BE']


def get_data_from_sim(file_name):
    data = []
    f = open(file_name)
    for index, line in enumerate(f):
        line_list = line.split()
        cols = len(line_list)
        if index == 0:
            for i in range(cols - 1):
                data.append([])
        if index > 0:
            for i in range(0, cols - 1):
                data[i].append(float(line_list[i + 1]))

    f.close()
    return data


def io_plot(plot_data, save_img=False):
    fig = plt.figure(figsize=(8, 5))
    ax = fig.add_subplot(111)
    xs = np.arange(0, len(plot_data[0]), 1)
    count = 0
    for ys in plot_data:
        if count < (len(plot_data) - 1):
            ax.plot(xs, ys, linewidth=2, color=color_styles[count], linestyle=linestyles[count], marker=markers[count], label=client_labels[count])
        count = count + 1

    # ax.xaxis.set_major_locator(plt.MultipleLocator(50))
    #ax.yaxis.set_major_locator(plt.MultipleLocator(10))
    ax.set(xlim=(0, len(xs) - 1), ylim=(0, 5200))
    # ax.legend(loc='center right')

    font1 = {'family': 'Times New Roman',
             'weight': 'normal',
             'size': 16,
             }
    ncol = int(math.ceil(6/2.))
    plt.legend(ncol=ncol, loc='upper center', prop=font1)
    plt.grid(axis='y', color='0.7', linestyle=':')
    plt.tick_params(labelsize=16)
    labels = ax.get_xticklabels() + ax.get_yticklabels()
    [label.set_fontname('Times New Roman') for label in labels]
    ax.set_xlabel("Time (sec)", font1)
    ax.set_ylabel("IOPS", font1)

    if save_img:
        date_str = datetime.now().strftime("%y%m%d%H%M%S")
        plt.savefig("sim_plot" + str(date_str) + ".svg")
    plt.show()


if __name__ == '__main__':
    data = get_data_from_sim("../ai_platform/ai_sim_631_iops_f.log")
    io_plot(data, True)
    # print(data)
