# NNUE Training (CPU)

This is a minimal end-to-end workflow using the NNUE tools added to this repo.

## 1) Build

```bash
cmake -S . -B build
cmake --build build -j
```

## 2) Sample and shard positions

```bash
./build/nnue_sample \
  --dataset /path/to/lichess_db_eval.jsonl \
  --out nnue_shard.bin \
  --max-positions 100000 \
  --target-depth 8 \
  --mate-plies-max 4 \
  --cp-abs-max 1200 \
  --pv-swing-max 400 \
  --min-nonpawn 6 \
  --max-nonpawn 12
```

## 3) Train (float model)

```bash
./build/nnue_train \
  --shard nnue_shard.bin \
  --epochs 3 \
  --lr 0.002 \
  --lr-decay 0.85 \
  --label-scale 2000 \
  --grad-clip 5 \
  --weight-clip 3 \
  --weight-decay 1e-3 \
  --hidden-clip 5 \
  --acc-dim 256 \
  --h1-dim 256 \
  --h2-dim 32 \
  --checkpoint nnue_ckpt \
  --out nnue_float.bin
```

To continue training from an existing float model while changing decay/clipping:

```bash
./build/nnue_train \
  --shard nnue_shard.bin \
  --resume nnue_float.bin \
  --epochs 2 \
  --lr 0.001 \
  --weight-clip 3 \
  --weight-decay 5e-4 \
  --out nnue_float.bin
```

## 4) Export to engine NNUE format

```bash
./build/nnue_export \
  --in nnue_float.bin \
  --out nnue.bin \
  --auto-scale \
  --auto-input \
  --dataset /path/to/lichess_db_eval.jsonl \
  --auto-max 20000 \
  --target-p95 600 \
  --target-hidden-p95 64
```

## 5) Validate vs classic eval (optional)

```bash
./build/nnue_validate \
  --dataset /path/to/lichess_db_eval.jsonl \
  --nnue nnue.bin \
  --target-depth 8 \
  --max-positions 1000
```

## 6) Use NNUE in the engine (UCI)

```text
setoption name NNUEFile value /home/harl/projects/chess-engine/nnue.bin
setoption name UseNNUE value true
```

Notes:
- `--max-positions` keeps sampling and validation quick.
- Filtering flags are tuned to avoid immediate mates and unstable tactical leaf nodes.
