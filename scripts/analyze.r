#! /usr/bin/env Rscript

library(data.table)

printf <- function(...) cat(sprintf(...))

import <- function(file) {
	dt <- data.table::fread(file)
	dt[, nsec:=nsec/1000]
	stats <- dt[order(size), list(n=length(nsec), min=min(nsec), max=max(nsec),
			   mean=mean(nsec), sd=sd(nsec),
			   median=as.double(median(nsec)),
			   q25=quantile(nsec, probs=c(.25)),
			   q75=quantile(nsec, probs=c(.75))), by=size]
	return(stats)
}

shim <- import("shim.txt")
warp <- import("warp.txt")

shim
warp

xmax <- max(shim$size, warp$size)
ymax <- max(shim$mean + shim$sd/2, warp$mean + warp$sd/2, shim$q75, warp$q75)#, shim$max, warp$max)
xr <- c(0, xmax)
yr <- c(0, ymax)

plot(0, type="n", xlim=xr, ylim=yr,
     xlab="UDP payload size [bytes]", ylab=expression(paste("RTT [", mu, "s]")),
     main=sprintf("Mean RTTs over %d iterations", shim$n[1]))
grid()
par(new=T)
plot(shim$size, shim$mean, xlim=xr, ylim=yr, axes=FALSE, ann=FALSE, type="o", col="blue")
segments(shim$size, shim$mean-shim$sd/2, shim$size, shim$mean+shim$sd/2, col="blue")
par(new=T)
plot(warp$size, warp$mean, xlim=xr, ylim=yr, axes=FALSE, ann=FALSE, type="o", col="red")
segments(warp$size, warp$mean-warp$sd/2, warp$size, warp$mean+warp$sd/2, col="red")
legend("topleft", c("shim", "warp"), col=c("blue", "red"), lty=1)

plot(0, type="n", xlim=xr, ylim=yr,
     xlab="UDP payload size [bytes]", ylab=expression(paste("RTT [", mu, "s]")),
     main=sprintf("Median RTTs over %d iterations", shim$n[1]))
grid()
par(new=T)
plot(shim$size, shim$median, xlim=xr, ylim=yr, axes=FALSE, ann=FALSE, type="o", col="blue")
segments(shim$size, shim$q25, shim$size, shim$q75, col="blue")
par(new=T)
plot(warp$size, warp$median, xlim=xr, ylim=yr, axes=FALSE, ann=FALSE, type="o", col="red")
segments(warp$size, warp$q25, warp$size, warp$q75, col="red")
legend("topleft", c("shim", "warp"), col=c("blue", "red"), lty=1)

