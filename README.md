
# EWSFS
![Amount of Badges](https://img.shields.io/badge/badges-too_many-blue)
![GitHub License](https://img.shields.io/github/license/gstaaij/ewsfs)
![GitHub Issues or Pull Requests](https://img.shields.io/github/issues/gstaaij/ewsfs)
![GitHub Issues or Pull Requests](https://img.shields.io/github/issues-pr/gstaaij/ewsfs)
![GitHub language count](https://img.shields.io/github/languages/count/gstaaij/ewsfs)
![GitHub top language](https://img.shields.io/github/languages/top/gstaaij/ewsfs)
![GitHub last commit](https://img.shields.io/github/last-commit/gstaaij/ewsfs)

The [**E**soteric](https://en.wiktionary.org/wiki/esoteric_programming_language#English), **W**orking and **S**olid **F**ile **S**ystem

## Setup

```console
$ git clone https://github.com/gstaaij/ewsfs
$ cd ewsfs
$ gcc -o nob nob.c
$ mkdir build
$ truncate -s 2560008 build/fs.img
$ ./mkfs.ewsfs build/fs.img
$ ./nob mount build/fs.img
```
