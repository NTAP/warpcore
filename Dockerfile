FROM alpine:latest
RUN apk add --no-cache cmake ninja gcc g++ musl-dev linux-headers
ADD . /src
WORKDIR /src/Debug
RUN cmake -GNinja -DDOCKER=True -DCMAKE_INSTALL_PREFIX=/dst ..
RUN ninja install

FROM alpine:latest
COPY --from=0 /dst /
EXPOSE 55555/UDP
CMD ["sockinetd", "-i", "eth0"]
