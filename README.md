[![Coverity Scan Build Status](https://scan.coverity.com/projects/2187/badge.svg)](https://scan.coverity.com/projects/2187)

About
=====
This "tc-server" project is to build an NFS server supporting "transactional
compound" of NFSv4. The goal of this project is to achieve, simultaneously,
greater performance and simpler API, by leveraging vectorization and
transaction execution of file system operations across network.

The client-side change is finished and reported in the [FAST2017
paper][vNFS-talk] entitled ["vNFS: Maximizing NFS Performance with Compounds
and Vectorized I/O"][vNFS-pdf]. This project focus on the server side. That is
to build NFS server that can transactionally execute NFS compounds issued by
the vNFS client. This project is built on top of NFS-Ganesha.

Notice
======
The "fsl-" prefix of this project means this is a private repository internal
to [File systems and Storage Lab][fsl-web] (FSL) in the Computer Science
Department at Stony Brook University. Although it will eventually be
open-source, this repository should be kept private before formal publication
of the work (e.g., in an academic conference with a paper reporting this
project).

NFS-Ganesha
===========

NFS-Ganesha is an NFSv3,v4,v4.1 fileserver that runs in user mode on most
UNIX/Linux systems.  It also supports the 9p.2000L protocol.

For more information, consult the [project wiki](https://github.com/nfs-ganesha/nfs-ganesha/wiki).


[vNFS-talk]: https://www.usenix.org/conference/fast17/technical-sessions/presentation/chen
[vNFS-pdf]: http://www.fsl.cs.sunysb.edu/docs/nfs4perf/vnfs-fast17.pdf
[fsl-web]: http://www.fsl.cs.stonybrook.edu/
