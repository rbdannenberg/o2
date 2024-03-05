README.txt - for o2litepy

o2litepy by Zekai Shen and Roger B. Dannenberg
Mar 2024

FOR PYTHON

To build the Python package:
% make help
(package is built in py3pkg/dist)

For documentation, see py3pkg/README.md 

FOR MICROPYTHON

For documentation of the o2litepy module, see py3pkg/README.md  

My development process is described here:
    https://www.cs.cmu.edu/~rbd/blog/upyesp/upesp-blog4mar2024.html

To install on MicroPython, I suggest using the following directly in
MicroPython:
    import mip
    mip.install("github:rbdannenberg/o2/o2litepy")

