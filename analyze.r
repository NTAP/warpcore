printf <- function(...) cat(sprintf(...))

ximport <- function(kind) {
	tmp <- c()
	for (f in list.files(pattern=paste(kind, ".*.txt", sep=""))) {
		data <- read.table(f, header=TRUE)
		size <- data$size[1]
		n <- length(data$us)
		mean <- mean(data$us)
		sd <- sd(data$us)
		median <- median(data$us)
		q25 <- quantile(data$us, probs = c(.25))
		q75 <- quantile(data$us, probs = c(.75))
	        # printf("%s %d %f\n", kind, size, mean)
		tmp$size <- append(tmp$size, size)
		tmp$n <- append(tmp$n, n)
		tmp$mean <- append(tmp$mean, mean)
		tmp$sd <- append(tmp$sd, sd)
		tmp$median <- append(tmp$median, median)
		tmp$q25 <- append(tmp$q25, q25)
		tmp$q75 <- append(tmp$q75, q75)
	}
	tmp <- data.frame(size=tmp$size, n=tmp$n, mean=tmp$mean, sd=tmp$sd,
	                  median=tmp$median, q25=tmp$q25, q75=tmp$q75)
	tmp <- tmp[order(tmp$size), ]
	return(tmp)
}

kern <- ximport("kern")
warp <- ximport("warp")

kern
warp

xmax <- max(kern$size, warp$size)
ymax <- max(kern$mean + kern$sd/2, warp$mean + warp$sd/2)
xr <- c(0, xmax)
yr <- c(0, ymax)

plot(0, type="n", xlim=xr, ylim=yr,
     xlab="Packet size [bytes]", ylab=expression(paste("RTT [", mu, "s]")),
     main=sprintf("Mean RTTs over %d iterations", kern$n[1]))
grid()
par(new=T)
plot(kern$size, kern$mean, ylim=yr, axes=FALSE, ann=FALSE, type="o", col="blue")
segments(kern$size, kern$mean-kern$sd/2, kern$size, kern$mean+kern$sd/2)
par(new=T)
plot(warp$size, warp$mean, ylim=yr, axes=FALSE, ann=FALSE, type="o", col="red")
segments(warp$size, warp$mean-warp$sd/2, warp$size, warp$mean+warp$sd/2)


xmax <- max(kern$size, warp$size)
ymax <- max(kern$q75, warp$q75)
xr <- c(0, xmax)
yr <- c(0, ymax)

plot(0, type="n", xlim=xr, ylim=yr,
     xlab="Packet size [bytes]", ylab=expression(paste("RTT [", mu, "s]")),
     main=sprintf("Median RTTs over %d iterations", kern$n[1]))
grid()
par(new=T)
plot(kern$size, kern$median, ylim=yr, axes=FALSE, ann=FALSE, type="o", col="blue")
segments(kern$size, kern$q25, kern$size, kern$q75)
par(new=T)
plot(warp$size, warp$median, ylim=yr, axes=FALSE, ann=FALSE, type="o", col="red")
segments(warp$size, warp$q25, warp$size, warp$q75)

