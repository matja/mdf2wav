Convert a RAW CDDA (Red Book / Mode 2) disk image with subchannel data to WAV
files for each track using the subcode 'P' channel to identify track positions.

Each output file will be named "track_XX.wav" and written in the current
directory, where XX is the track number starting from 01.

compile:
  make

run:
  ./mdf2wav < disk.mdf
