#! /usr/bin/env Rscript

library(ggplot2)
library(data.table)
library(tools)

files <- list.files(pattern=".*ping.*.txt")

my_fread <-function(file) {
    item <- fread(file)
    tags <- strsplit(file_path_sans_ext(file), "[-.]")[[1]]
    item$file <- file
    item$method <- tags[1] # ifelse(tags[1] == "shimping", "Kernel Stack", "Warpcore")
    item$speed <- tags[2] # paste(tags[2], "G")
    item$busywait <- ifelse(tags[3] %in% "b", "busy-wait", "poll()")
    item$zcksum <- ifelse(tags[3] %in% "z" | tags[4] %in% "z",
                          "zero-checksum", "checksum")
    return(item)
}

l <- lapply(files, my_fread)
dt <- rbindlist(l)
dt[, usec:=nsec/1000]

stats <- dt[, list(n=length(usec), min=min(usec), max=max(usec),
                   mean=mean(usec), sd=sd(usec), median=as.double(median(usec)),
                   q1=quantile(usec, probs=c(.01)),
                   q25=quantile(usec, probs=c(.25)),
                   q75=quantile(usec, probs=c(.75)),
                   q99=quantile(usec, probs=c(.99))),
            by=list(method, speed, busywait, zcksum, size)]

my_plot<- function(dt, ymax) {
    theme <- theme(
            axis.line.x = element_line(size = 0.2, colour = "#777777"),
            axis.line.y = element_line(size = 0.2, colour = "#777777"),
            axis.text=element_text(family="Times", size=7, color="black"),
            axis.ticks.x=element_line(colour="#777777", size=.2),
            axis.ticks.y=element_line(colour="#777777", size=.2),
            axis.title=element_text(family="Times", size=7, color="black"),
            legend.background=element_blank(),
            legend.key.height=unit(7, "pt"),
            legend.key.width=unit(7, "mm"),
            legend.key=element_blank(),
            legend.position=c(.7, .18),
            legend.text=element_text(family="Times", size=7, color="black"),
            legend.title=element_blank(),
            panel.background=element_blank(),
            panel.border = element_blank(),
            panel.grid.major.x=element_line(colour="#777777", size=.2,
                                            linetype="dotted"),
            panel.grid.major.y=element_line(colour="#777777", size=.2,
                                            linetype="dotted"),
            plot.margin=unit(c(0,0,0,0),"mm"),
            strip.text=element_text(family="Times", size=7, color="black"),
            text=element_text(family="Times", size=7, color="black")
    )

    plot <- ggplot(data=dt, aes(x=size, y=median,
                                   shape=paste(busywait, "+", zcksum),
                                   color=paste(busywait, "+", zcksum))) +
            geom_errorbar(aes(ymin=q1, ymax=q99), linetype="solid", size=.25,
                          position=position_dodge(width=20)) +
            geom_line(size=.5) +
            geom_point(size=1) +
            scale_colour_brewer(type="div", palette="PuOr") +
            scale_x_continuous(expand=c(0, 0), limit=c(0, 1560),
                               name="Packet Size [B]") +
            scale_y_continuous(expand=c(0, 0), limit=c(0, ymax),
                               name=expression(paste("Median RTT [", mu, "s]")))
    return(plot + theme)
}

for (s in unique(stats$speed)) {
    fstats <- stats[stats$speed == s]
    ymax <- max(fstats$q99)
    for (m in unique(fstats$method)) {
        ffstats <- fstats[fstats$method == m]
        print(ffstats)
        ggsave(plot=my_plot(ffstats, ymax), height=1.75, width=3.5, units="in",
               filename=paste(m, "-", s, ".pdf", sep=""))
    }
}
