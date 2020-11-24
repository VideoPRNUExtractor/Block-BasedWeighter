# VideoPRNUextractor
Loop filter compensation and block based PRNU weighting for H264 and HEVC coded videos. See Ref [1] for more details. The tool provides two main capabilities:

1.  Compensation for effects of loop filtering procedure applied at encoder and decoder.
2.  Loop filter compensation and PRNU weighting applied based on encoding settings (block locations, sizes, and quantization parameters)


It must be noted that for stabilized videos loop filter compensation must be applied but application of PRNU weighting can be performed only after inversion of stabilization transformations. Stabilization support will soon be incorporated Ref [2].


Extractor:  Extracts loop filter compensated video frames
FFMPEG:  	modified FFMPEG libraries
.m:      Estimates PRNU weighted pattern 

## Requirements
- This project works on the Linux architecture. The code was tested on Ubuntu version 18.04
- Install Sqlite
- Install Matlab
- Check if you have Java installed on your system with `java -version`
- If you do not have java, install it by running `sudo apt install default-jre` command.
- Clone git repository: `git clone https://github.com/VideoPRNUExtractor/Weighter.git`

## FFMPEG Installation
- Change your working directory to FFMPEG directory. `cd Weighter/FFMPEG/`
- Install H264 library by running `sudo apt-get install libx264-dev`
- HEVC installation can be a problem with default apt-get therefor first download the latest version of x265 from [link](http://ftp.videolan.org/pub/videolan/x265/ "link")
	- Extrcat tar file and change your working directory to x265_your_version. `cd x265_1.5`
	- Install Yasm with `sudo apt-get install yasm`
	- Install cmake with `sudo apt-get install mercurial cmake cmake-curses-gui build-essential yasm`
	- Configure to x265 by running `./make-Makefiles.bash`; after that press c for configure then g for generate.
	- Run `make` command.
 		- Some versions interestingly contain string error in two files. If you see that you can change this line according to GCC output.
	- If you have libx265 remove it by running `sudo apt-get autoremove libx265-dev`
	- Install x265 with `sudo make install`
	- Update to library folders by running `sudo ldconfig`
- Change your working directory to FFMPEG directory.
- Install package config with running `sudo apt install pkg-config`
- Configure your FFMPEG by running ` ./configure --disable-optimizations --disable-mmx --disable-stripping --enable-debug=3 --disable-asm --extra-cflags="-gstabs+" --enable-libx264 --enable-gpl --enable-libx265`
- Run `sudo make examples`
- Run `sudo make install`
 - If you change any code in FFMPEG you must run this code again.

### Testing FFMPEG
- You can download a raw video from [link](https://media.xiph.org/video/derf/ "link")
- You can use downloaded video as a input file to creat a video with running `ffmpeg -i football_cif.y4m  -c:v libx264 -preset slow -crf 22 -c:a copy  -vframes 120 football_cif.mkv`

------------

## Installation of Extractor

- If you do not have sqlite, install it by running `sudo apt-get install sqlite3 libsqlite3-dev`
- Change your folder to the Extractor folder.
- Run `sudo make install`

## Usage

### Loop Filter Compansation

Extractor takes two input, first one is a video path with its name and second one is an integer database control variable. If this variable equals 0, it does not save database information to the database, otherwise it saves all video information to the database.

- This function creates a folder with appaned a "Frame" string to video's path. This folder containes loop filter compansated frames of video.
- Example usage of extractor without database information  can be obtained running `./extract_mvs video_path 0`.

### Weighting Usage

You can open getFingerprintWeighted.m in Matlab. This function extracts both loop filter compensated video frames and their block's QP values. It extracts PRNU fingerprint from video with weighting.

- The function requires to functions and filters in [Goljan, Fridrich](http://dde.binghamton.edu/download/camera_fingerprint/ "Goljan, Fridrich")
- The function has 3 input values for respectively video path, extractor path, and weight values.
- The function returns weighted fingerprint.

### Dataset

Dataset described in Ref [3] can be found at [Dataset](https://drive.google.com/file/d/1hkgXvQDOxokGrLKQaaROt45AKOz25FiZ/view?usp=sharing). This dataset contains 47 videos captured using various Android smartphone cameras through a custom built camera app that allows capturing videos at the highest possible bitrate  (i.e., corresponding to QP=1) while turning off stabilization and electronic zoom to correctly determine the weighting function without interference from other incamera post-processing. All videos include indoor scenes captured  under  natural light by moving the cameras at the highest supported frame resolution of the camera by limiting the duration of each video to 4 seconds.

## References

Ref [1]: E. Altınışık, K. Taşdemir, and H. T. Sencar, "Mitigation of H.264 and H.265 Video Compression for Reliable PRNU Estimation” IEEE Transactions on Information Forensics and Security, 2019. [.PDF](https://ieeexplore.ieee.org/document/8854840 ".PDF")

Ref [2]: E. Altınışık, and H. T. Sencar, "Source Camera Verification for Strongly Stabilized Videos." IEEE Transactions on Information Forensics and Security 16 (2020). [.PDF](https://ieeexplore.ieee.org/abstract/document/9169924 ".PDF") 

Ref [3]: E. Altınışık, K. Taşdemir, and H. T. Sencar,  "PRNU Estimation from Encoded Videos Using Block-Based Weighting." arXiv preprint arXiv:2008.08138 [.PDF](https://arxiv.org/pdf/2008.08138.pdf ".PDF")
