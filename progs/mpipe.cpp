// -*- encoding: utf-8 -*-
//
//  Copyright(C) 2010-2012 Taro Watanabe <taro.watanabe@nict.go.jp>
//

#include <cstdio>

#include <iostream>
#include <string>
#include <vector>
#include <stdexcept>
#include <iterator>
#include <algorithm>

#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
#include <boost/thread.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/algorithm/string.hpp>

#include <utils/mpi.hpp>
#include <utils/mpi_stream.hpp>
#include <utils/mpi_stream_simple.hpp>
#include <utils/async_device.hpp>
#include <utils/compress_stream.hpp>
#include <utils/lockfree_list_queue.hpp>
#include <utils/subprocess.hpp>
#include "utils/lexical_cast.hpp"

struct MapReduce
{
  typedef size_t id_type;
  typedef std::pair<id_type, std::string> value_type;
  typedef utils::lockfree_list_queue<value_type, std::allocator<value_type> > queue_type;
  typedef utils::lockfree_list_queue<id_type, std::allocator<id_type> > queue_id_type;

  typedef utils::subprocess subprocess_type;
};


namespace std
{
  inline
  void swap(MapReduce::value_type& x, MapReduce::value_type& y)
  {
    std::swap(x.first,  y.first);
    std::swap(x.second, y.second);
  }
};

struct Mapper
{
  typedef MapReduce map_reduce_type;
  
  typedef map_reduce_type::id_type         id_type;
  typedef map_reduce_type::value_type      value_type;
  typedef map_reduce_type::queue_type      queue_type;
  typedef map_reduce_type::queue_id_type   queue_id_type;
  typedef map_reduce_type::subprocess_type subprocess_type;

  Mapper(queue_type& __queue,
	 queue_id_type& __queue_id,
	 subprocess_type& __subprocess)
    : queue(__queue),
      queue_id(__queue_id),
      subprocess(__subprocess)
  {}

  void operator()()
  {
    boost::iostreams::filtering_ostream os;
    os.push(utils::async_sink(subprocess.desc_write(), true));
    subprocess.desc_write() = -1;
    
    value_type value;
    
    while (1) {
      queue.pop_swap(value);
      if (value.first == id_type(-1)) break;
      
      queue_id.push(value.first);
      
      os << value.second << '\n';
    }
    
    queue_id.push(id_type(-1));
  }

  queue_type&      queue;
  queue_id_type&   queue_id;
  subprocess_type& subprocess;
};

struct Reducer
{
  typedef MapReduce map_reduce_type;
  
  typedef map_reduce_type::id_type         id_type;
  typedef map_reduce_type::value_type      value_type;
  typedef map_reduce_type::queue_type      queue_type;
  typedef map_reduce_type::queue_id_type   queue_id_type;
  typedef map_reduce_type::subprocess_type subprocess_type;
  
  Reducer(queue_type& __queue,
	  queue_id_type& __queue_id,
	  subprocess_type& __subprocess)
    : queue(__queue),
      queue_id(__queue_id),
      subprocess(__subprocess)
  {}

  void operator()()
  {
    boost::iostreams::filtering_istream is;
    is.push(utils::async_source(subprocess.desc_read(), true));
    subprocess.desc_read() = -1;
    
    value_type value;
        
    while (1) {
      queue_id.pop(value.first);
      if (value.first == id_type(-1)) break;
      
      if (! std::getline(is, value.second))
	throw std::runtime_error("invalid lines?");
      
      queue.push_swap(value);
    }
    
    value.first  = id_type(-1);
    value.second = std::string();
    
    queue.push_swap(value);
  }
  
  queue_type&      queue;
  queue_id_type&   queue_id;
  subprocess_type& subprocess;
};

struct Consumer
{
  typedef MapReduce map_reduce_type;
  
  typedef map_reduce_type::id_type         id_type;
  typedef map_reduce_type::value_type      value_type;
  typedef map_reduce_type::queue_type      queue_type;

  Consumer(queue_type& __queue,
	   std::istream& __is)
    : queue(__queue),
      is(__is)
  {}
  
  void operator()()
  {
    id_type id = 0;
    std::string line;
    
    while (std::getline(is, line)) {
      queue.push(std::make_pair(id, line));
      ++ id;
    }
    
    queue.push(std::make_pair(id_type(-1), std::string()));
  }
  
  queue_type&   queue;
  std::istream& is;
};

struct Merger
{
  typedef MapReduce map_reduce_type;
 
  typedef map_reduce_type::id_type         id_type;
  typedef map_reduce_type::value_type      value_type;
  typedef map_reduce_type::queue_type      queue_type;
  
  Merger(queue_type& __queue,
	 std::ostream& __os,
	 const int __mpi_size)
    : queue(__queue),
      os(__os),
      mpi_size(__mpi_size) {}

  void operator()()
  {
    typedef std::map<id_type, std::string, std::less<id_type>, std::allocator<std::pair<const id_type, std::string> > > value_set_type;

    value_type value;
    value_set_type values;
    
    id_type curr = 0;
    int consumed_size = 0;
    
    while (1) {
      queue.pop_swap(value);
      if (value.first == id_type(-1)) {
	++ consumed_size;
	if (consumed_size == mpi_size)
	  break;
	else
	  continue;
      }
      
      if (curr == value.first) {
	os << value.second << '\n';
	++ curr;
      } else 
	values.insert(value);
      
      while (! values.empty() && values.begin()->first == curr) {
	os << values.begin()->second << '\n';
	values.erase(values.begin());
	++ curr;
      }
    }
    
    while (! values.empty() && values.begin()->first == curr) {
      os << values.begin()->second << '\n';
      values.erase(values.begin());
      ++ curr;
    }

    if (! values.empty()) {
      std::cerr << "invalid lines: expected: " << curr << " current: " << values.begin()->first << std::endl;
      
      throw std::runtime_error("invalid id: expected: " + utils::lexical_cast<std::string>(curr) + " current: " + utils::lexical_cast<std::string>(values.begin()->first));
    }
  }
  
  queue_type&   queue;
  std::ostream& os;
  int mpi_size;
};

enum {
  line_tag = 1000,
  notify_tag,
};

inline
void tokenize(const std::string& buffer, MapReduce::value_type& value)
{
  typedef MapReduce::id_type id_type;
  
  std::string::const_iterator iter = buffer.begin();

  for (/**/; iter != buffer.end() && ! std::isspace(*iter); ++ iter);
  
  value.first = utils::lexical_cast<id_type>(buffer.substr(0, iter - buffer.begin()));
  value.second = buffer.substr(iter + 1 - buffer.begin());
}


inline
int loop_sleep(bool found, int non_found_iter)
{
  if (! found) {
    boost::thread::yield();
    ++ non_found_iter;
  } else
    non_found_iter = 0;
    
  if (non_found_iter >= 50) {
    struct timespec tm;
    tm.tv_sec = 0;
    tm.tv_nsec = 2000001;
    nanosleep(&tm, NULL);
    
    non_found_iter = 0;
  }
  return non_found_iter;
}

typedef boost::filesystem::path path_type;

path_type input_file = "-";
path_type output_file = "-";
std::string command;

int debug = 0;

int getoptions(int argc, char** argv);


int main(int argc, char** argv)
{
  utils::mpi_world mpi_world(argc, argv);
  
  const int mpi_rank = MPI::COMM_WORLD.Get_rank();
  const int mpi_size = MPI::COMM_WORLD.Get_size();
  
  try {
    if (getoptions(argc, argv) != 0) 
      return 1;

    if (command.empty())
      throw std::runtime_error("no command?");

    typedef MapReduce map_reduce_type;
      
    typedef map_reduce_type::id_type         id_type;
    typedef map_reduce_type::value_type      value_type;
    typedef map_reduce_type::queue_type      queue_type;
    typedef map_reduce_type::queue_id_type   queue_id_type;
    typedef map_reduce_type::subprocess_type subprocess_type;

    typedef Mapper  mapper_type;
    typedef Reducer reducer_type;
    
    typedef Consumer consumer_type;
    typedef Merger   merger_type;
    
    if (mpi_rank == 0) {
      subprocess_type subprocess(command);
      
      queue_type    queue_is(mpi_size);
      queue_type    queue_send(1);
      queue_id_type queue_id;
      queue_type    queue_recv;
      
      const bool flush_output = (output_file == "-"
				 || (boost::filesystem::exists(output_file)
				     && ! boost::filesystem::is_regular_file(output_file)));
      
      utils::compress_istream is(input_file, 1024 * 1024);
      utils::compress_ostream os(output_file, 1024 * 1024 * (! flush_output));

      boost::thread consumer(consumer_type(queue_is, is));
      boost::thread merger(merger_type(queue_recv, os, mpi_size));

      boost::thread mapper(mapper_type(queue_send, queue_id, subprocess));
      boost::thread reducer(reducer_type(queue_recv, queue_id, subprocess));
      
      typedef utils::mpi_ostream        ostream_type;
      typedef utils::mpi_istream_simple istream_type;

      typedef boost::shared_ptr<ostream_type> ostream_ptr_type;
      typedef boost::shared_ptr<istream_type> istream_ptr_type;
      
      typedef std::vector<ostream_ptr_type, std::allocator<ostream_ptr_type> > ostream_ptr_set_type;
      typedef std::vector<istream_ptr_type, std::allocator<istream_ptr_type> > istream_ptr_set_type;
      
      ostream_ptr_set_type ostream(mpi_size);
      istream_ptr_set_type istream(mpi_size);
      
      for (int rank = 1; rank < mpi_size; ++ rank) {
	ostream[rank].reset(new ostream_type(rank, line_tag, 4096));
	istream[rank].reset(new istream_type(rank, line_tag, 4096));
      }
      
      std::string line;
      value_type value(0, std::string());
      value_type value_recv(0, std::string());
      
      int non_found_iter = 0;
      
      while (value.first != id_type(-1)) {
	bool found = false;
	
	for (int rank = 1; rank < mpi_size && value.first != id_type(-1); ++ rank)
	  if (ostream[rank]->test() && queue_is.pop(value, true) && value.first != id_type(-1)) {
	    ostream[rank]->write(utils::lexical_cast<std::string>(value.first) + ' ' + value.second);
	    
	    found = true;
	  }
	
	if (queue_send.empty() && queue_is.pop(value, true) && value.first != id_type(-1)) {
	  queue_send.push(value);
	  
	  found = true;
	}
	
	// reduce...
	for (int rank = 1; rank < mpi_size; ++ rank)
	  if (istream[rank] && istream[rank]->test()) {
	    if (istream[rank]->read(line)) {
	      tokenize(line, value_recv);
	      
	      queue_recv.push_swap(value_recv);
	    } else {
	      queue_recv.push(std::make_pair(id_type(-1), std::string()));
	      istream[rank].reset();
	    }
	    
	    found = true;
	  }
	
	non_found_iter = loop_sleep(found, non_found_iter);
      }
      
      bool terminated = false;
      
      for (;;) {
	bool found = false;
	
	if (! terminated && queue_send.push(std::make_pair(id_type(-1), std::string()), true)) {
	  terminated = true;
	  
	  found = true;
	}
	
	// termination...
	for (int rank = 1; rank < mpi_size; ++ rank)
	  if (ostream[rank] && ostream[rank]->test()) {
	    if (! ostream[rank]->terminated())
	      ostream[rank]->terminate();
	    else
	      ostream[rank].reset();
	    
	    found = true;
	  }
	
	// reduce...
	for (int rank = 1; rank < mpi_size; ++ rank)
	  if (istream[rank] && istream[rank]->test()) {
	    if (istream[rank]->read(line)) {
	      tokenize(line, value_recv);
	      
	      queue_recv.push_swap(value_recv);
	    } else {
	      queue_recv.push(std::make_pair(id_type(-1), std::string()));
	      istream[rank].reset();
	    }
	    
	    found = true;
	  }
	
	// termination condition!
	if (std::count(istream.begin(), istream.end(), istream_ptr_type()) == mpi_size
	    && std::count(ostream.begin(), ostream.end(), ostream_ptr_type()) == mpi_size
	    && terminated) break;
	
	non_found_iter = loop_sleep(found, non_found_iter);
      }
      
      mapper.join();
      reducer.join();
      consumer.join();
      merger.join();
    } else {
      subprocess_type subprocess(command);
      
      queue_type    queue_send(1);
      queue_id_type queue_id;
      queue_type    queue_recv;
      
      boost::thread mapper(mapper_type(queue_send, queue_id, subprocess));
      boost::thread reducer(reducer_type(queue_recv, queue_id, subprocess));

      typedef utils::mpi_istream        istream_type;
      typedef utils::mpi_ostream_simple ostream_type;
      
      boost::shared_ptr<istream_type> is(new istream_type(0, line_tag, 4096));
      boost::shared_ptr<ostream_type> os(new ostream_type(0, line_tag, 4096));
      
      std::string line;
      value_type value;

      bool terminated = false;
      
      int non_found_iter = 0;
      for (;;) {
	bool found = false;
	
	if (is && is->test() && queue_send.empty()) {
	  if (is->read(line))
	    tokenize(line, value);
	  else {
	    value.first  = id_type(-1);
	    value.second = std::string();
	    
	    is.reset();
	  }
	  
	  queue_send.push_swap(value);
	  
	  found = true;
	}
	
	if (! terminated) {
	  if (os && os->test() && queue_recv.pop_swap(value, true)) {
	    if (value.first == id_type(-1))
	      terminated = true;
	    else
	      os->write(utils::lexical_cast<std::string>(value.first) + ' ' + value.second);
	    
	    found = true;
	  }
	} else {
	  if (os && os->test()) {
	    if (! os->terminated())
	      os->terminate();
	    else
	      os.reset();
	    found = true;
	  }
	}
	
	if (! is && ! os) break;
	
	non_found_iter = loop_sleep(found, non_found_iter);
      }
      
      mapper.join();
      reducer.join();
    }
    
    // synchronize...
    if (mpi_rank == 0) {
      std::vector<MPI::Request, std::allocator<MPI::Request> > request_recv(mpi_size);
      std::vector<MPI::Request, std::allocator<MPI::Request> > request_send(mpi_size);
      std::vector<bool, std::allocator<bool> > terminated_recv(mpi_size, false);
      std::vector<bool, std::allocator<bool> > terminated_send(mpi_size, false);
    
      terminated_recv[0] = true;
      terminated_send[0] = true;
      for (int rank = 1; rank != mpi_size; ++ rank) {
	request_recv[rank] = MPI::COMM_WORLD.Irecv(0, 0, MPI::INT, rank, notify_tag);
	request_send[rank] = MPI::COMM_WORLD.Isend(0, 0, MPI::INT, rank, notify_tag);
      }
    
      int non_found_iter = 0;
      for (;;) {
	bool found = false;
      
	for (int rank = 1; rank != mpi_size; ++ rank)
	  if (! terminated_recv[rank] && request_recv[rank].Test()) {
	    terminated_recv[rank] = true;
	    found = true;
	  }
      
	for (int rank = 1; rank != mpi_size; ++ rank)
	  if (! terminated_send[rank] && request_send[rank].Test()) {
	    terminated_send[rank] = true;
	    found = true;
	  }
      
	if (std::count(terminated_send.begin(), terminated_send.end(), true) == mpi_size
	    && std::count(terminated_recv.begin(), terminated_recv.end(), true) == mpi_size) break;
      
	non_found_iter = loop_sleep(found, non_found_iter);
      }
    } else {
      MPI::Request request_send = MPI::COMM_WORLD.Isend(0, 0, MPI::INT, 0, notify_tag);
      MPI::Request request_recv = MPI::COMM_WORLD.Irecv(0, 0, MPI::INT, 0, notify_tag);
    
      bool terminated_send = false;
      bool terminated_recv = false;
    
      int non_found_iter = 0;
      for (;;) {
	bool found = false;
      
	if (! terminated_send && request_send.Test()) {
	  terminated_send = true;
	  found = true;
	}
      
	if (! terminated_recv && request_recv.Test()) {
	  terminated_recv = true;
	  found = true;
	}
      
	if (terminated_send && terminated_recv) break;
      
	non_found_iter = loop_sleep(found, non_found_iter);
      }
    }
  }
  catch (const std::exception& err) {
    std::cerr << "error: " << argv[0] << " "<< err.what() << std::endl;
    MPI::COMM_WORLD.Abort(1);
    return 1;
  }
  return 0;
}

int getoptions(int argc, char** argv)
{
  const int mpi_rank = MPI::COMM_WORLD.Get_rank();
  const int mpi_size = MPI::COMM_WORLD.Get_size();

  namespace po = boost::program_options;
  
  po::options_description desc("options");
  desc.add_options()
    ("input",  po::value<path_type>(&input_file)->default_value(input_file),   "input file")
    ("output", po::value<path_type>(&output_file)->default_value(output_file), "output file")
    ("command", po::value<std::string>(&command),                              "command")
    ("debug",  po::value<int>(&debug)->implicit_value(1), "debug level")
    ("help", "help message");
  
  po::variables_map vm;
  po::store(po::parse_command_line(argc, argv, desc, po::command_line_style::unix_style & (~po::command_line_style::allow_guessing)), vm);
  //po::store(po::command_line_parser(argc, argv).options(cmdline_options).positional(pos).run(), vm);
  po::notify(vm);
  
  if (vm.count("help")) {
    if (mpi_rank == 0)
      std::cout << argv[0] << " [options]" << '\n' << desc << '\n';
    return 1;
  }
  
  return 0;
}
