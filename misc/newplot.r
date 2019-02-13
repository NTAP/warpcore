#! /usr/bin/env Rscript

using <- function(...) {
    libs <- unlist(list(...))
    req <- unlist(lapply(libs, require, character.only=TRUE))
    need <- libs[req==FALSE]
    if(length(need) > 0){
        install.packages(need, repos = "https://cloud.r-project.org/")
        lapply(need, require, character.only=TRUE)
    }
}

.libPaths(new="~/.R")
using("tools", "cowplot", "scales", "tidyverse")

options(width=255)

my_fread = function(file) {
    item = read_tsv(file, col_types=cols(byte=col_integer(), pkts=col_integer(),
                                        tx=col_integer(), rx=col_integer()))
    tags = strsplit(file_path_sans_ext(file), "[-.]")[[1]]
    item$file = file
    item$method = tags[1]
    item$speed = tags[2]
    item$busywait = if (tags[3] %in% "b") "busy-wait" else "poll()"
    item$zcksum =
        if (tags[3] %in% "z" | tags[4] %in% "z") "zero-checksum" else "checksum"
    item
}

dt = bind_rows(lapply(list.files(pattern=".*ping.*.txt"), my_fread))

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
  dt$group = paste(dt$busywait, "+", dt$zcksum)

  grouped = group_by(dt, method, speed, byte)
  med = summarise(grouped, median=median(rx))
  maxlat = max(med$median, na.rm=TRUE)

  plots = list()
  i = 1

  total = length(unique(dt$method)) * length(unique(dt$speed))
  for (m in unique(dt$method)) {
    dm = filter(dt, dt$method == m)
    for (s in unique(dt$speed)) {
      d = filter(dm, dm$speed == s)
      # Eth preamble/crc/gap + Eth hdr + IP hdr + UDP hdr
      d$byte = d$byte + (d$pkts * (24 + 14 + 20 + 8))

      if (grepl("Gb", ylabel, fixed=TRUE))
        ymax = as.numeric(s)/8
      else if (grepl("RTT", ylabel, fixed=TRUE))
        ymax = maxlat
      else
        ymax = NA
      plot = ggplot(data=d, aes_string(x=x, y=y, shape="group", color="group")) +
              stat_summary(fun.y=median, geom="line") +
              stat_summary(fun.y=median, geom="point") +
              scale_colour_brewer(type="div", palette="PuOr", drop=FALSE) +
              scale_x_continuous(labels=shortb, limit=c(16, NA),
                                 name=ifelse(i > total/2, xlabel, ""),
                                 expand=c(0.02, 0), # trans='log10'
                                 ) +
              scale_y_continuous(labels=ylabeller, expand=c(0, 0),
                                 limit=c(0, ymax),
                                 name=ifelse(i == 1 | i == (total/2) + 1,
                                 ylabel, ""))
      plot = plot + theme_cowplot(font_size=6, font_family="Times") +
             background_grid(major = "y", minor = "none")
      if (i == total/2)
        plots[[i]] = plot + theme(legend.position = c(0, .8),
                                  legend.title=element_blank(),
                                  legend.key.height=unit(2.5, "mm"))
      else
        plots[[i]] = plot + theme(legend.position="none")
      i = i + 1
    }
  }
  plot_grid(plotlist=plots, nrow=2, ncol=total/2)
}


# save_plot(plot=my_plot(dt, "byte", "rx", "UDP Payload Size [B]",
#                     expression(paste("RTT [", mu, "s]")), usec),
#           base_height=1.8, base_width=7.15, units="in",
#           filename="latency.pdf")

save_plot(plot=my_plot(dt, "byte", "2*byte/rx", "UDP Payload Size [B]",
                    "Throughput [Gb/s]", gbps),
          base_height=2, base_width=7.15, units="in",
          filename="thruput.pdf")

# save_plot(plot=my_plot(dt, "pkts", "2*pkts/rx", "Packets [#]",
#                     "Packet Rate [Mp/s]", mpps),
#           base_height=1.8, base_width=7.15, units="in",
#           filename="pps.pdf")

short = filter(dt, dt$byte < 1600)

save_plot(plot=my_plot(short, "byte", "rx", "UDP Payload Size [B]",
                    expression(paste("RTT [", mu, "s]")), usec),
          base_height=2, base_width=7.15, units="in",
          filename="latency-1500.pdf")

# save_plot(plot=my_plot(short, "byte", "2*byte/rx",
#                     "UDP Payload Size [B]",
#                     "Throughput [Gb/s]", gbps),
#           base_height=1.8, base_width=7.15, units="in",
#           filename="thruput-1500.pdf")

file.remove("Rplots.pdf")
