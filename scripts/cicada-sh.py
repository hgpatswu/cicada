#!/usr/bin/env python
#
#  Copyright(C) 2010-2012 Taro Watanabe <taro.watanabe@nict.go.jp>
#
###
### a wrapper script for running multiple commands
### inspired by mpish and thrsh, we support pbs 
### Actually, we will run by mpish, thrsh and pbs
###

import threading
import multiprocessing

import time
import sys
import os, os.path
import string
import re
import subprocess
import shutil

import UserList
import UserString
import cStringIO

from optparse import OptionParser, make_option

opt_parser = OptionParser(
    option_list=[
    ## max-malloc
    make_option("--max-malloc", default=4, action="store", type="float",
                metavar="MALLOC", help="maximum memory in GB (default: 4)"),

    # CICADA Toolkit directory
    make_option("--cicada-dir", default="", action="store", type="string",
                metavar="DIRECTORY", help="cicada directory"),
    # MPI Implementation.. if different from standard location...
    make_option("--mpi-dir", default="", action="store", type="string",
                metavar="DIRECTORY", help="MPI directory"),

    make_option("--threads", default=2, action="store", type="int",
                help="# of thrads for thread-based parallel processing"),
    # perform threading or MPI training    
    make_option("--mpi", default=0, action="store", type="int",
                help="# of processes for MPI-based parallel processing. Identical to --np for mpirun"),
    make_option("--mpi-host", default="", action="store", type="string",
                help="list of hosts to run job. Identical to --host for mpirun", metavar="HOSTS"),
    make_option("--mpi-host-file", default="", action="store", type="string",
                help="host list file to run job. Identical to --hostfile for mpirun", metavar="FILE"),
    make_option("--pbs", default=None, action="store_true",
                help="PBS for launching processes"),
    make_option("--pbs-queue", default="ltg", action="store", type="string",
                help="PBS queue for launching processes (default: ltg)", metavar="NAME"),
    make_option("--pbs-after", default="", action="store", type="string",
                help="PBS for launching processes after this process id", metavar="ID"),
    make_option("--pbs-before", default="", action="store", type="string",
                help="PBS for launching processes before this process id", metavar="ID"),

    ## debug messages
    make_option("--debug", default=0, action="store", type="int"),
    ])

def run_command(command):
    retcode = subprocess.Popen(command, shell=True).wait()
    if retcode < 0:
        sys.exit(retcode)

def compressed_file(file):
    if not file:
        return file
    if os.path.exists(file):
        return file
    if os.path.exists(file+'.gz'):
	return file+'.gz'
    if os.path.exists(file+'.bz2'):
	return file+'.bz2'
    (base, ext) = os.path.splitext(file)
    if ext == '.gz' or ext == '.bz2':
	if os.path.exists(base):
	    return base
    return file

class QSUB(multiprocessing.Process):
    def __init__(self, command=""):
        multiprocessing.Process.__init__(self)
        self.command = command
        
    def run(self):
        popen = subprocess.Popen("qsub -S /bin/sh", shell=True, stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        popen.communicate(self.command)
        
class PBS:
    def __init__(self, queue="", workingdir=os.getcwd()):
        self.queue = queue
        self.workingdir = workingdir

        self.workers = []

    def __del__(self):
        for worker in self.workers:
            worker.join()
            
    def run(self, command="", threads=1, memory=0.0, name="cicada-sh", logfile=None, after="", before=""):
        pipe = cStringIO.StringIO()
        
        pipe.write("#!/bin/sh\n")
        pipe.write("#PBS -N %s\n" %(name))
        pipe.write("#PBS -e /dev/null\n")
        pipe.write("#PBS -o /dev/null\n")
        pipe.write("#PBS -W block=true\n")

        if after:
            pipe.write("#PBS -W depend=after:%s\n" %(after))

        if before:
            pipe.write("#PBS -W depend=before:%s\n" %(before))
        
        if self.queue:
            pipe.write("#PBS -q %s\n" %(self.queue))
            
        if memory > 0.0:
            if memory < 1.0:
                pipe.write("#PBS -l select=1:ncpus=%d:mpiprocs=1:mem=%dmb\n" %(threads, int(memory * 1000)))
            else:
                pipe.write("#PBS -l select=1:ncpus=%d:mpiprocs=1:mem=%dgb\n" %(threads, int(memory)))
        else:
            pipe.write("#PBS -l select=1:ncpus=%d:mpiprocs=1\n" %(threads))

        # setup variables
        if os.environ.has_key('TMPDIR_SPEC'):
            pipe.write("export TMPDIR_SPEC=%s\n" %(os.environ['TMPDIR_SPEC']))
        if os.environ.has_key('LD_LIBRARY_PATH'):
            pipe.write("export LD_LIBRARY_PATH=%s\n" %(os.environ['LD_LIBRARY_PATH']))
        if os.environ.has_key('DYLD_LIBRARY_PATH'):
            pipe.write("export DYLD_LIBRARY_PATH=%s\n" %(os.environ['DYLD_LIBRARY_PATH']))
            
        pipe.write("cd \"%s\"\n" %(self.workingdir))
        
        if logfile:
            pipe.write("%s >& %s\n" %(command, logfile))
        else:
            pipe.write("%s\n" %(command))

        self.workers.append(QSUB(pipe.getvalue()))
        self.workers[-1].start()

class Threads:
    
    def __init__(self, cicada=None, threads=1):
        command = "%s" %(cicada.thrsh)
        command += " --threads %d" %(threads)
        command += " --debug"
        
        self.popen = subprocess.Popen(command, shell=True, stdin=subprocess.PIPE)
        self.pipe = self.popen.stdin
        
    def __del__(self):
        self.pipe.close()
        self.popen.wait()

    def run(self, command=""):
        self.pipe.write("%s\n" %(command))
        self.pipe.flush()

class MPI:
    
    def __init__(self, cicada=None, dir="", hosts="", hosts_file="", number=0):
        
	self.dir = dir
	self.hosts = hosts
        self.hosts_file = hosts_file
        self.number = number
	
        if self.dir:
            if not os.path.exists(self.dir):
                raise ValueError, self.dir + " does not exist"
            self.dir = os.path.realpath(self.dir)

        if self.hosts_file:
            if not os.path.exists(self.hosts_file):
                raise ValueError, self.hosts_file + " does no exist"
            self.hosts_file = os.path.realpath(hosts_file)

        self.bindir = self.dir
	
        for binprog in ['mpirun']:
            if self.bindir:
                prog = os.path.join(self.bindir, 'bin', binprog)
                if not os.path.exists(prog):
                    prog = os.path.join(self.bindir, binprog)
                    if not os.path.exists(prog):
                        raise ValueError, prog + " does not exist at " + self.bindir
                    
                setattr(self, binprog, prog)
            else:
                setattr(self, binprog, binprog)
        
        command = self.mpirun
        if self.number > 0:
            command += ' --np %d' %(self.number)
            
        if self.hosts:
            command += ' --host %s' %(self.hosts)
        elif self.hosts_file:
            command += ' --hostfile %s' %(self.hosts_file)

        if os.environ.has_key('TMPDIR_SPEC'):
            mpirun += ' -x TMPDIR_SPEC'
        if os.environ.has_key('LD_LIBRARY_PATH'):
            mpirun += ' -x LD_LIBRARY_PATH'
        if os.environ.has_key('DYLD_LIBRARY_PATH'):
            mpirun += ' -x DYLD_LIBRARY_PATH'

        command += " %s" %(cicada.mpish)
        command += " --debug"
        
        self.popen = subprocess.Popen(command, shell=True, stdin=subprocess.PIPE)
        self.pipe = self.popen.stdin
        
    def __del__(self):
        self.pipe.close()
        self.popen.wait()
        
    def run(self, command=""):
        self.pipe.write("%s\n" %(command))
        self.pipe.flush()


class CICADA:
    def __init__(self, dir=""):
        self.bindirs = []
        
        if not dir:
            dir = os.path.abspath(os.path.dirname(__file__))
            if dir:
                self.bindirs.append(dir)
                
                dir = os.path.dirname(dir)
                if dir:
                    self.bindirs.append(dir)
        
	self.dir = dir
	if not os.path.exists(self.dir):
	    raise ValueError, self.dir + " does not exist"
	self.dir = os.path.realpath(self.dir)

	for dir in ('bin', 'progs', 'scripts'): 
	    bindir = os.path.join(self.dir, dir)
	    if os.path.exists(bindir) and os.path.isdir(bindir):
		self.bindirs.append(bindir)
        self.bindirs.append(self.dir)
	
        for binprog in ('mpish', ### mpi-launcher
                        'thrsh', ### thread-launcher
                        ):
	    
	    for bindir in self.bindirs:
		prog = os.path.join(bindir, binprog)
                
                if not os.path.exists(prog): continue
                if os.path.isdir(prog): continue
                
                setattr(self, binprog, prog)
                break

	    if not hasattr(self, binprog):
		raise ValueError, binprog + ' does not exist'

(options, args) = opt_parser.parse_args()

if options.pbs:
    # we use pbs to run jobs
    pbs = PBS(queue=options.pbs_queue)
    
    for line in sys.stdin:
        line = line.strip()
        if line:
            pbs.run(command=line, threads=options.threads, memory=options.max_malloc, after=options.pbs_after, before=options.pbs_before)

elif options.mpi:
    cicada = CICADA(options.cicada_dir)
    mpi = MPI(cicada=cicada,
              dir=options.mpi_dir,
              hosts=options.mpi_host,
              hosts_file=options.mpi_host_file,
              number=options.mpi)
    
    for line in sys.stdin:
        line = line.strip()
        if line:
            mpi.run(command=line)
else:
    cicada = CICADA(options.cicada_dir)
    threads = Threads(cicada=cicada, threads=options.threads)
    
    for line in sys.stdin:
        line = line.strip()
        if line:
            threads.run(command=line)
