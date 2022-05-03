FROM debian:buster-slim as builder
RUN apt-get -q update && apt-get install -qy build-essential

WORKDIR /app
COPY master_mel ./master_mel/
COPY serial_frame ./serial_frame/
COPY Inc ./Inc/
RUN cd master_mel && make

FROM debian:buster-slim
COPY --from=builder /app/master_mel/master_mel /app/master_mel

WORKDIR /app
ENTRYPOINT /app/master_mel --dev /dev/ttyACM* --listen --udp

