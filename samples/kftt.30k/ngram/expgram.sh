#!/bin/sh

expgram=

if test "$expgram" = ""; then
  echo "where is your expgram?"
  exit 1
fi

### Following is a quick example of LM estimation on a small data set.

$expgram/progs/expgram_counts_extract --corpus ../data/train.en.bz2 --output ngram.5.en.counts --order 5 --threads 4

$expgram/progs/expgram_counts_estimate --ngram ngram.5.en.counts --output ngram.5.en.lm --shard 4

### A standard way is:
# $expgram/scripts/expgram.py --corpus ../data/train.en --output ngram.5.en --threads 4
