# Shared Tokenizer Report

- tokenizer type: `sentencepiece_bpe_v1`
- trainer backend: `sentencepiece`
- normalizer: `english_project_v1`
- requested vocab size: `512`
- learned vocab size: `512`
- special tokens: `<bos>=1`, `<eos>=2`, `<pad>=3`, `<unk>=0`
- training texts used: `9`
- held-out texts used for inspection: `3`
- repeated training examples after weighting: `13`
- total training characters: `819`
- normalization rules: `project normalize in C++ before SentencePiece identity normalization`
- bounded sampled training input cap: `5,000,000` lines by default
- average characters per token on held-out sample: `2.915`
- average tokens per held-out example: `35.333`
- byte fallback tokens on held-out sample: `0`
- unknown tokens on held-out sample: `0` (byte fallback enabled)

## Source Files

- `shard-000000.txt` kind=`parquet_corpus_text` included=`true` records=`2` texts=`2` checksum=`fnv1a64:29e764441154d042`
- `supplemental-conversations.txt` kind=`parquet_corpus_text` included=`true` records=`1` texts=`1` checksum=`fnv1a64:479d94138d741d34`
- `exports/base_training/base_training.jsonl` kind=`base_training` included=`true` records=`1` texts=`1` checksum=`fnv1a64:a16b883b1bce8dfe`
- `exports/contextualization/contextualization.jsonl` kind=`contextualization` included=`true` records=`2` texts=`2` checksum=`fnv1a64:4e1ad5642cfef834`
- `exports/custom_verbalization/custom_verbalization.jsonl` kind=`custom_verbalization` included=`true` records=`1` texts=`2` checksum=`fnv1a64:1d741530d20c9393`
- `exports/small_supervised/small_supervised.jsonl` kind=`small_supervised` included=`true` records=`2` texts=`1` checksum=`fnv1a64:64ce4c9713d93361`
- `C:/MINA/tokenizer/tests/fixtures/tokenizer_repo/dataset` kind=`corpus_inventory` included=`false` records=`1` texts=`0` checksum=`fnv1a64:3bdf829d065a818e`

## Notes

- The shared tokenizer is intended for both shell tracks, so runtime consumers should load the same model artifact rather than training shell-specific vocabularies.
- The scalable C++ trainer streams tokenizer-ready shard text from the configured corpus-text root, stages only the small project-specific JSONL-derived text mix in scratch space, and materializes one bounded sampled SentencePiece input file.
- For large corpora, the trainer now enforces the sentence cap before SentencePiece starts, rather than relying on SentencePiece to sample from the iterator input.
- Raw Parquet extraction remains a separate corpus-preparation step handled by tokenizer_parquet_ingest_tool.
