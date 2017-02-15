#! /usr/bin/env Rscript

library(ggplot2)
library(data.table)
library(tools)

files <- list.files(pattern=".*ping.*.txt")

my_fread <-function(file) {
    item <- fread(file)
    tags <- strsplit(file_path_sans_ext(file), "[-.]")[[1]]
    item$file <- file
    item$method <- tags[1] # if(tags[1] == "shimping", "Kernel Stack", "Warpcore")
    item$speed <- tags[2] # paste(tags[2], "G")
    item$busywait <- if (tags[3] %in% "b") "busy-wait" else "poll()"
    item$zcksum <-
        if (tags[3] %in% "z" | tags[4] %in% "z") "zero-checksum" else "checksum"
    return (item)
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

my_plot<- function(dt, ymax, legend) {
    theme <- theme(
            axis.line.x=element_line(size=0.2, colour="#777777"),
            axis.line.y=element_line(size=0.2, colour="#777777"),
            axis.text=element_text(family="Times", size=7, color="black"),
            axis.ticks.x=element_line(colour="#777777", size=.2),
            axis.ticks.y=element_line(colour="#777777", size=.2),
            axis.title.x=element_text(margin=margin(-.1, 0, 0, 0)),
            axis.title.y=element_text(margin=margin(0, -.1, 0, 0)),
            axis.title=element_text(family="Times", size=7, color="black"),
            legend.background=element_blank(),
            legend.key.height=unit(7, "pt"),
            legend.key.width=unit(7, "mm"),
            legend.key=element_blank(),
            legend.position=if (legend) c(.75, .25) else "none",
            legend.text=element_text(family="Times", size=7, color="black"),
            legend.title=element_blank(),
            panel.background=element_blank(),
            panel.border=element_blank(),
            panel.grid.major.x=element_line(colour="#777777", size=.2,
                                            linetype="dotted"),
            panel.grid.major.y=element_line(colour="#777777", size=.2,
                                            linetype="dotted"),
            plot.margin=unit(c(0, 0, 0, 0), "mm"),
            strip.text=element_text(family="Times", size=7, color="black"),
            text=element_text(family="Times", size=7, color="black")
    )

    plot <- ggplot(data=dt, aes(x=size, y=median,
                                   shape=paste(busywait), #, "+", zcksum),
                                   color=paste(busywait))) + #, "+", zcksum))) +
            geom_errorbar(aes(ymin=q1, ymax=q99), linetype="solid", size=.25,
                          # position=position_dodge(width=20)
                          width=20) +
            geom_line(size=.5) +
            geom_point(size=1) +
            # scale_colour_brewer(type="div", palette="PuOr") +
            scale_x_continuous(expand=c(0, 0), limit=c(0, 1551),
                               name="Packet Size [B]") +
            scale_y_continuous(expand=c(0, 0), limit=c(0, ymax),
                               name=expression(paste("Median RTT [", mu, "s]")))
    return (plot + theme)
}

ymax <- max(stats$q99)
legend <- TRUE
for (s in unique(stats$speed)) {
    for (m in unique(stats$method)) {
        fstats <- stats[stats$speed == s & stats$method == m]
        print(fstats[, grep(c("speed|method|busywait|zcksum|^n|size|median|q1|q99"),
                            names(fstats)), with = FALSE])
        ggsave(plot=my_plot(fstats, ymax, legend),
               height=1.3, width=3.5, units="in",
               filename=paste(m, "-", s, ".pdf", sep=""))
        # legend <- FALSE
    }
    # compute some stats for the text


}
