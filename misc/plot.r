#! /usr/bin/env Rscript

library(ggplot2)
library(data.table)
library(tools)
library(scales)

options(width=255)

files <- list.files(pattern=".*ping.*.txt")

my_fread <-function(file) {
    item <- fread(file)
    tags <- strsplit(file_path_sans_ext(file), "[-.]")[[1]]
    item$file <- file
    item$method <- tags[1]
    item$speed <- tags[2]
    item$busywait <- if (tags[3] %in% "b") "busy-wait" else "poll()"
    item$zcksum <-
        if (tags[3] %in% "z" | tags[4] %in% "z") "zero-checksum" else "checksum"
    # drop the first measurement, as it is often artificially long
    item <- item[-1,]
    return(item)
}

l <- lapply(files, my_fread)
dt <- rbindlist(l)

speed_labeller <- function(string) { return(paste0(string, "G")) }

method_labeller <- function(string) {
  return(ifelse(string == "shimping", "Kernel Stack", "Warpcore"))
}

gbps <- function(bytepnsec) { return(comma(bytepnsec*8)) }

usec <- function(nsec) { return(comma(nsec/1000)) }

mpps <- function(ppnsec) { return(comma(ppnsec*1000)) }


myplot <- function(dt, x, y, xlabel, ylabel, ymax, ylabeller) {
  theme <- theme(
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
          legend.position=c(.12, .9),
          legend.text=element_text(family="Times", size=7, color="black"),
          legend.title=element_blank(),
          panel.background=element_blank(),
          panel.spacing=unit(0.15, "in"),
          strip.background=element_rect(colour="white", fill="white"),
          panel.grid.major.x=element_line(colour="#777777", size=.2,
                                          linetype="dotted"),
          panel.grid.major.y=element_line(colour="#777777", size=.2,
                                          linetype="dotted"),
          plot.margin=unit(c(0, 0, 0, 0), "mm"),
          strip.text=element_text(family="Times", size=7, color="black"),
          text=element_text(family="Times", size=7, color="black")
  )

  dt$group <- paste(dt$busywait, "+", dt$zcksum)
  q25 <- function(x) {quantile(x, probs=0.25)}
  q75 <- function(x) {quantile(x, probs=0.75)}
  plot <- ggplot(data=dt, aes_string(x=x, y=y, shape="group", color="group")) +
          facet_grid(method ~ speed,
                     labeller=labeller(speed=speed_labeller,
                                       method=method_labeller)) +
          geom_smooth(size=.5, alpha=.25, method="loess") +
          # stat_summary(fun.y=median, fun.ymin=q25, fun.ymax=q75, size=.25) +
          stat_summary(fun.data=mean_se, size=.25) +
          scale_colour_brewer(type="div", palette="PuOr", drop=FALSE) +
          scale_x_continuous(labels=comma, expand=c(0, 0), limit=c(0, NA),
                             name=xlabel) +
          scale_y_continuous(labels=ylabeller, expand=c(0, 0),
                             limit=c(0, ymax), name=ylabel)
    return(plot + theme)
}


common <- c()

ggsave(plot=myplot(dt, "byte", "rx", "UDP Payload Size [B]",
                   expression(paste("RTT [", mu, "s]")), NA, usec),
       height=2.75, width=7.15, units="in", filename="latency.pdf")

ggsave(plot=myplot(dt, "byte", "byte/rx", "UDP Payload Size [B]",
                   "Throughput [GB/s]", NA, gbps),
       height=2.75, width=7.15, units="in", filename="thruput.pdf")

ggsave(plot=myplot(dt, "pkts", "pkts/tx", "Packets [#]",
                   "Packets/Second [Mpps]", NA, mpps),
       height=2.75, width=7.15, units="in", filename="pps.pdf")

short <- dt[dt$byte < 1600]

ggsave(plot=myplot(short, "byte", "rx", "UDP Payload Size [B]",
                   expression(paste("RTT [", mu, "s]")), NA, usec),
       height=2.75, width=7.15, units="in", filename="latency-1500.pdf")

ggsave(plot=myplot(short, "byte", "byte/rx", "UDP Payload Size [B]",
                   "Throughput [GB/s]", NA, gbps),
       height=2.75, width=7.15, units="in", filename="thruput-1500.pdf")
