#! /usr/bin/env Rscript

library(data.table)

printf <- function(...) cat(sprintf(...))

import <- function(file) {
	dt <- data.table::fread(file)
  dt[, time:=time/1000]
	stats <- dt[order(size), list(n=length(time), min=min(time), max=max(time),
			   mean=mean(time), sd=sd(time),
			   median=as.double(median(time)),
			   q25=quantile(time, probs=c(.25)),
			   q75=quantile(time, probs=c(.75)),
			   cmean=mean(code), csd=sd(code)), by=size]
	return(stats)
}

kern <- import("kern.txt")
warp <- import("warp.txt")

kern
warp

xmax <- max(kern$size, warp$size)
ymax <- max(kern$mean + kern$sd/2, warp$mean + warp$sd/2, kern$q75, warp$q75)#, kern$max, warp$max)
xr <- c(0, xmax)
yr <- c(0, ymax)

plot(0, type="n", xlim=xr, ylim=yr,
     xlab="UDP payload size [bytes]", ylab=expression(paste("RTT [", mu, "s]")),
     main=sprintf("Mean RTTs over %d iterations", kern$n[1]))
grid()
par(new=T)
plot(kern$size, kern$mean, xlim=xr, ylim=yr, axes=FALSE, ann=FALSE, type="o", col="blue")
segments(kern$size, kern$mean-kern$sd/2, kern$size, kern$mean+kern$sd/2, col="blue")
par(new=T)
plot(warp$size, warp$mean, xlim=xr, ylim=yr, axes=FALSE, ann=FALSE, type="o", col="red")
segments(warp$size, warp$mean-warp$sd/2, warp$size, warp$mean+warp$sd/2, col="red")
legend("topleft", c("kern", "warp"), col=c("blue", "red"), lty=1)

plot(0, type="n", xlim=xr, ylim=yr,
     xlab="UDP payload size [bytes]", ylab=expression(paste("RTT [", mu, "s]")),
     main=sprintf("Median RTTs over %d iterations", kern$n[1]))
grid()
par(new=T)
plot(kern$size, kern$median, xlim=xr, ylim=yr, axes=FALSE, ann=FALSE, type="o", col="blue")
segments(kern$size, kern$q25, kern$size, kern$q75, col="blue")
par(new=T)
plot(warp$size, warp$median, xlim=xr, ylim=yr, axes=FALSE, ann=FALSE, type="o", col="red")
segments(warp$size, warp$q25, warp$size, warp$q75, col="red")
legend("topleft", c("kern", "warp"), col=c("blue", "red"), lty=1)

