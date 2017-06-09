#! /usr/bin/env Rscript

library(ggplot2)
library(data.table)
library(tools)
library(scales)

options(width=255)

my_fread = function(file) {
    item = fread(file)
    tags = strsplit(file_path_sans_ext(file), "[-.]")[[1]]
    item$file = file
    item$method = tags[1]
    item$speed = tags[2]
    item$busywait = if (tags[3] %in% "b") "busy-wait" else "poll()"
    item$zcksum =
        if (tags[3] %in% "z" | tags[4] %in% "z") "zero-checksum" else "checksum"
    item
}

dt = rbindlist(lapply(list.files(pattern=".*ping.*.txt"), my_fread))


speed_lab = function(string) { paste0(string, "G Ethernet") }

method_lab = function(string) {
  ifelse(string == "sockping", "Kernel Stack", "Warpcore")
}

gbps = function(bytepnsec) { comma(bytepnsec*8) }

usec = function(nsec) { comma(nsec/1000) }

mpps = function(ppnsec) { comma(ppnsec*1000) }

shortb = function(byte) {
  ifelse(byte >= 10^9, paste(comma(byte/10^9), "G"),
         ifelse(byte >= 10^6, paste(comma(byte/10^6), "M"),
                ifelse(byte >= 10^3, paste(comma(byte/10^3), "K"),
                       comma(byte))))
}

my_plot = function(dt, x, y, xlabel, ylabel, ylabeller) {
  theme = theme(
    axis.line.x=element_line(size=0.2, colour="#777777"),
    axis.line.y=element_line(size=0.2, colour="#777777"),
    axis.text=element_text(family="Times", size=7, color="black"),
    axis.ticks.x=element_line(colour="#777777", size=.2),
    axis.ticks.y=element_line(colour="#777777", size=.2),
    axis.title=element_text(family="Times", size=7, color="black"),
    legend.background=element_blank(),
    legend.key.height=unit(7, "pt"),
    legend.key.width=unit(7, "mm"),
    legend.key=element_blank(),
    legend.position=c(.12, .3),
    legend.text=element_text(family="Times", size=7, color="black"),
    legend.title=element_blank(),
    panel.background=element_blank(),
    panel.grid.major.x=element_line(colour="#777777", size=.2,
                                    linetype="dotted"),
    panel.grid.major.y=element_line(colour="#777777", size=.2,
                                    linetype="dotted"),
    panel.spacing=unit(0.15, "in"),
    plot.margin=unit(c(0, 0, 0, 0), "mm"),
    strip.background=element_rect(colour="white", fill="white"),
    strip.text=element_text(family="Times", size=7, color="black"),
    text=element_text(family="Times", size=7, color="black")
  )

  dt$group = paste(dt$busywait, "+", dt$zcksum)
  q25 = function(x) { quantile(x, probs=0.25) }
  q75 = function(x) { quantile(x, probs=0.75) }
  plot = ggplot(data=dt, aes_string(x=x, y=y, shape="group", color="group")) +
          facet_grid(method ~ speed,
                     labeller=labeller(speed=speed_lab, method=method_lab)) +
          # geom_smooth(size=.5, alpha=.25, method="loess") +
          stat_summary(fun.y=median, geom="line") +
          stat_summary(fun.y=median, geom="point", size=1.5) +
          scale_colour_brewer(type="div", palette="PuOr", drop=FALSE) +
          scale_x_continuous(labels=shortb, expand=c(0, 0), limit=c(0, NA),
                             name=xlabel) +
          scale_y_continuous(labels=ylabeller, expand=c(0, 0),
                             limit=c(0, NA), name=ylabel) +
          guides(color=guide_legend(override.aes=list(fill=NA)))
    plot + theme
}



ggsave(plot=my_plot(dt, "byte", "rx", "UDP Payload Size [B]",
                    expression(paste("RTT [", mu, "s]")), usec),
       height=1.75, width=7.15, units="in", filename="latency.pdf")

ggsave(plot=my_plot(dt, "byte", "2*byte/rx", "UDP Payload Size [B]",
                    "Throughput [GB/s]", gbps),
       height=1.75, width=7.15, units="in", filename="thruput.pdf")

ggsave(plot=my_plot(dt, "pkts", "2*pkts/rx", "Packets [#]",
                    "Packets/Second [Mpps]", mpps),
       height=1.75, width=7.15, units="in", filename="pps.pdf")

short = dt[dt$byte < 1600]

ggsave(plot=my_plot(short, "byte", "rx", "UDP Payload Size [B]",
                    expression(paste("RTT [", mu, "s]")), usec),
       height=1.75, width=7.15, units="in", filename="latency-1500.pdf")

ggsave(plot=my_plot(short, "byte", "2*byte/rx",
                    "UDP Payload Size [B]",
                    "Throughput [GB/s]", gbps),
       height=1.75, width=7.15, units="in", filename="thruput-1500.pdf")
