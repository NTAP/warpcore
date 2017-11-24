FROM alpine:edge
RUN apk add --no-cache cmake ninja gcc g++ musl-dev linux-headers
ADD . /warpcore
WORKDIR /warpcore/Debug
RUN cmake -GNinja -DNO_SANITIZERS=True ..
RUN ninja

FROM alpine:edge
WORKDIR /warpcore/bin
COPY --from=0 /warpcore/Debug/bin/sock* ./
EXPOSE 55555/UDP
CMD ["./sockinetd", "-i", "eth0"]
