# Apache 性能测试报告

## 目的

本测试报告为个人笔记本apache性能测试报告，主要测试apache每秒能处理的事务数。

## 测试环境

服务器和客户机为同一机器（个人笔记本）：

*  Pentium(R) Dual-Core CPU T4300 @ 2.10GHz，内存：4GB RAM
*  操作系统：ubuntu 12.10
*  服务器：Apache/2.2.22
*  客户端：httptest

## 测试内容及方法

运行httptest，通过HTTP HEAD方法获取 [http://127.0.0.1/](http://127.0.0.1/)，不断提高每秒发送的连接数量，观察实际每秒的连接数量和成功率来估计本机服务器每秒能处理的事务数。

具体步骤，以100连接/s的初始速率运行httptest 20秒钟，记录最后5秒的实际平均速率以及总的成功率，然后以100为等差递增连接速率，分别记录实际平均速率以及成功率。（由于客户端运行不是很稳定，当程序持续运行一段时间之后可能异常退出select: Bad file descriptor，因此只持续运行20秒，取最后5秒的数据为实验数据。这是程序有待改进的地方）。

## 试验结果

<TABLE CELLPADDING=3 BORDER="1">
<TR><TD ALIGN="LEFT">设定连接速率</TD>
<TD ALIGN="LEFT">成功率</TD>
<TD ALIGN="LEFT">实际连接速率</TD>
<TD ALIGN="LEFT">&nbsp;</TD>
<TD ALIGN="LEFT">设定连接速率</TD>
<TD ALIGN="LEFT">成功率</TD>
<TD ALIGN="LEFT">实际连接速率</TD>
</TR>
<TR><TD ALIGN="LEFT">200/s</TD>
<TD ALIGN="LEFT">100%</TD>
<TD ALIGN="LEFT">200/s</TD>
<TD ALIGN="LEFT">&nbsp;</TD>
<TD ALIGN="LEFT">3000/s</TD>
<TD ALIGN="LEFT">99%</TD>
<TD ALIGN="LEFT">2905/s</TD>
</TR>
<TR><TD ALIGN="LEFT">400/s</TD>
<TD ALIGN="LEFT">100%</TD>
<TD ALIGN="LEFT">400/s</TD>
<TD ALIGN="LEFT">&nbsp;</TD>
<TD ALIGN="LEFT">3200/s</TD>
<TD ALIGN="LEFT">99%</TD>
<TD ALIGN="LEFT">3100/s</TD>
</TR>
<TR><TD ALIGN="LEFT">600/s</TD>
<TD ALIGN="LEFT">100%</TD>
<TD ALIGN="LEFT">600/s</TD>
<TD ALIGN="LEFT">&nbsp;</TD>
<TD ALIGN="LEFT">3400/s</TD>
<TD ALIGN="LEFT">99%</TD>
<TD ALIGN="LEFT">2900/s</TD>
</TR>
<TR><TD ALIGN="LEFT">600/s</TD>
<TD ALIGN="LEFT">99%</TD>
<TD ALIGN="LEFT">600/s</TD>
<TD ALIGN="LEFT">&nbsp;</TD>
<TD ALIGN="LEFT">3600/s</TD>
<TD ALIGN="LEFT">99%</TD>
<TD ALIGN="LEFT">2800/s</TD>
</TR>
<TR><TD ALIGN="LEFT">800/s</TD>
<TD ALIGN="LEFT">100%</TD>
<TD ALIGN="LEFT">800/s</TD>
<TD ALIGN="LEFT">&nbsp;</TD>
<TD ALIGN="LEFT">3800/s</TD>
<TD ALIGN="LEFT">99%</TD>
<TD ALIGN="LEFT">2982/s</TD>
</TR>
<TR><TD ALIGN="LEFT">1000/s</TD>
<TD ALIGN="LEFT">99%</TD>
<TD ALIGN="LEFT">1000/s</TD>
<TD ALIGN="LEFT">&nbsp;</TD>
<TD ALIGN="LEFT">4000/s</TD>
<TD ALIGN="LEFT">99%</TD>
<TD ALIGN="LEFT">2980/s</TD>
</TR>
<TR><TD ALIGN="LEFT">1200/s</TD>
<TD ALIGN="LEFT">99%</TD>
<TD ALIGN="LEFT">1200/s</TD>
<TD ALIGN="LEFT">&nbsp;</TD>
<TD ALIGN="LEFT">4200/s</TD>
<TD ALIGN="LEFT">99%</TD>
<TD ALIGN="LEFT">2854/s</TD>
</TR>
<TR><TD ALIGN="LEFT">1400/s</TD>
<TD ALIGN="LEFT">99%</TD>
<TD ALIGN="LEFT">1400/s</TD>
<TD ALIGN="LEFT">&nbsp;</TD>
<TD ALIGN="LEFT">4400/s</TD>
<TD ALIGN="LEFT">99%</TD>
<TD ALIGN="LEFT">3082/s</TD>
</TR>
<TR><TD ALIGN="LEFT">1600/s</TD>
<TD ALIGN="LEFT">99%</TD>
<TD ALIGN="LEFT">1595/s</TD>
<TD ALIGN="LEFT">&nbsp;</TD>
<TD ALIGN="LEFT">4600/s</TD>
<TD ALIGN="LEFT">99%</TD>
<TD ALIGN="LEFT">2890/s</TD>
</TR>
<TR><TD ALIGN="LEFT">1800/s</TD>
<TD ALIGN="LEFT">99%</TD>
<TD ALIGN="LEFT">1790/s</TD>
<TD ALIGN="LEFT">&nbsp;</TD>
<TD ALIGN="LEFT">4800/s</TD>
<TD ALIGN="LEFT">99%</TD>
<TD ALIGN="LEFT">2985/s</TD>
</TR>
<TR><TD ALIGN="LEFT">2000/s</TD>
<TD ALIGN="LEFT">99%</TD>
<TD ALIGN="LEFT">1991/s</TD>
<TD ALIGN="LEFT">&nbsp;</TD>
<TD ALIGN="LEFT">5000/s</TD>
<TD ALIGN="LEFT">99%</TD>
<TD ALIGN="LEFT">3082/s</TD>
</TR>
<TR><TD ALIGN="LEFT">2200/s</TD>
<TD ALIGN="LEFT">99%</TD>
<TD ALIGN="LEFT">2201/s</TD>
<TD ALIGN="LEFT">&nbsp;</TD>
<TD ALIGN="LEFT">5200/s</TD>
<TD ALIGN="LEFT">99%</TD>
<TD ALIGN="LEFT">3013/s</TD>
</TR>
<TR><TD ALIGN="LEFT">2400/s</TD>
<TD ALIGN="LEFT">98%</TD>
<TD ALIGN="LEFT">2395/s</TD>
<TD ALIGN="LEFT">&nbsp;</TD>
<TD ALIGN="LEFT">5400/s</TD>
<TD ALIGN="LEFT">99%</TD>
<TD ALIGN="LEFT">3080/s</TD>
</TR>
<TR><TD ALIGN="LEFT">2600/s</TD>
<TD ALIGN="LEFT">99%</TD>
<TD ALIGN="LEFT">2592/s</TD>
<TD ALIGN="LEFT">&nbsp;</TD>
<TD ALIGN="LEFT">5600/s</TD>
<TD ALIGN="LEFT">99%</TD>
<TD ALIGN="LEFT">2994/s</TD>
</TR>
<TR><TD ALIGN="LEFT">2800/s</TD>
<TD ALIGN="LEFT">99%</TD>
<TD ALIGN="LEFT">2798/s</TD>
<TD ALIGN="LEFT">&nbsp;</TD>
<TD ALIGN="LEFT">5800/s</TD>
<TD ALIGN="LEFT">99%</TD>
<TD ALIGN="LEFT">3030/s</TD>
</TR>
<TR><TD ALIGN="LEFT">&nbsp;</TD>
<TD ALIGN="LEFT">&nbsp;</TD>
<TD ALIGN="LEFT">&nbsp;</TD>
<TD ALIGN="LEFT">&nbsp;</TD>
<TD ALIGN="LEFT">6000/s</TD>
<TD ALIGN="LEFT">99%</TD>
<TD ALIGN="LEFT">3120/s</TD>
</TR>
</TABLE>

## 结果分析及改进

当设定的速率小于3000/s时，实际连接速率与设定速率十分接近。而当设定的速率大于3000/s时，实际连接速率与设定速率相差比较大，而且实际连接速率在3000/s左右波动。

然而由于客户端和服务器运行在同一台机器上，这并不能很好的反映服务器的处理能力。而且可能客户端httptest本身性能不够高，每秒无法发出足够的连接请求。因此还有待进一步的改进以及测试

* 将客户端和服务器分开
* 继续改进客户端性能
