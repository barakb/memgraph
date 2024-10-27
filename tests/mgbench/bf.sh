#!/usr/bin/env bash

cleanup() {
    echo "Cleaning up..."
    pkill -P $$
    # Or use: kill -- -$$
}

trap cleanup EXIT

python3 graph_bench.py --vendor falkordb ./run-falkordb.sh  --dataset-name demo --dataset-group "*" --dataset-size "*"
