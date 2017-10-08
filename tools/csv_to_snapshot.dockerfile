FROM debian:stretch

COPY csv_to_snapshot /usr/local/bin/csv_to_snapshot

# Setup memgraph user and group.
RUN groupadd -r memgraph
RUN useradd -lrm -g memgraph memgraph

# Setup intput/output directory.
# /data is used because that's the shortest way to reference the directory.
RUN mkdir -p /data
RUN chown -R memgraph:memgraph /data

# Change user and set working directory.
USER memgraph:memgraph
VOLUME /data
WORKDIR /data

ENTRYPOINT ["csv_to_snapshot"]
# Print help and usage by default, since at least one --nodes argument is
# required.
CMD ["--help"]
