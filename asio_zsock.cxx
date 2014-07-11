#include <iostream>
#include <vector>
#include <string>
#include <exception>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <zmq.h>


template <typename T>
void print(T&& t)
{
  std::cout << t << "\n";
}

template <typename First, typename... Args>
void print(First&& f, Args&&... args)
{
  std::cout << f << " ";
  print(std::forward<Args>(args)...);
}

namespace asio_zmq {



class zmq_exception: public std::exception {
public:
	zmq_exception (std::string __reason):
	reason_(__reason) {

	}
    const char* what() const noexcept {
    	return reason_.c_str();
    }
    std::string reason_;
};

class zmq_context {
public:
	typedef void* zmq_context_handle;
	zmq_context() {
		void *tmp = zmq_ctx_new ();
		if (tmp == NULL)
			throw zmq_exception("could not obtain a zmq context");
		handle_ = tmp;
	}
	zmq_context_handle handle() {
		return handle_;
	}
private:
	zmq_context_handle handle_;
};


void *get_zmq_context() {
	static zmq_context zmq_ctx_;
	return zmq_ctx_.handle(); 
}

template <typename ConstBufferSequence, typename Handler>
class zsock_send_op {
public:

	zsock_send_op(const void *__zmq_sock, const ConstBufferSequence & __buffers, Handler __handler): 
		buffers_(__buffers),
		handler_(__handler),
		zmq_sock_(__zmq_sock) 
	{
	}

	void operator()(const boost::system::error_code &ec, std::size_t bytes_transferred) {

		boost::system::error_code zec;
		std::size_t send_rc = 0;

		if (!ec) {
			int flags = 0;
			size_t fsize = sizeof (flags);
			zmq_getsockopt(const_cast<void *>(zmq_sock_), ZMQ_EVENTS, &flags, &fsize);

			if (flags & ZMQ_POLLOUT) {
				const void* buf = boost::asio::buffer_cast<const void*>(buffers_);
				int buf_size = boost::asio::buffer_size(buffers_);
			    send_rc = zmq_send (const_cast<void *>(zmq_sock_), 
			    	const_cast<void *>(buf), buf_size, 0);
			    
			    if (send_rc == -1)
			    {
			    	zec.assign(errno, boost::system::system_category());
			    }
			}
			handler_ (zec, send_rc);
		} // ec
		else {
			handler_ (ec, send_rc);	
		}
	}

private:
	ConstBufferSequence buffers_;
	Handler handler_;
	const void *zmq_sock_;
};


template <typename MutableBufferSequence, typename Handler>
class zsock_recv_op {
public:

	zsock_recv_op(void *__zmq_sock, const MutableBufferSequence & __buffers, Handler __handler): 
		buffers_(__buffers), 
		handler_(__handler),
		zmq_sock_(__zmq_sock) 
	{
	}

	void operator()(const boost::system::error_code &ec,  std::size_t bytes_transferred) {
		
		if (!ec) {
			boost::system::error_code zec; //defaut ctor no error
			int recvd = 0;
			int flags = 0;
			size_t fsize = sizeof (flags);
			zmq_getsockopt(const_cast<void *>(zmq_sock_), ZMQ_EVENTS, &flags, &fsize);
			if (flags & ZMQ_POLLIN) {
				//we can read from the zmq socket
				void *buf = boost::asio::buffer_cast<void *>(buffers_);
				int buf_size = boost::asio::buffer_size(buffers_);
				recvd = zmq_recv(zmq_sock_, buf, buf_size, 0);
				//check if recvd == -1
				if (recvd == -1) {
					zec.assign(errno, boost::system::system_category());
				}
			}
			//if we were not able to read it is not an error the application should try
			//again later
			handler_(zec, recvd);
		}
		else {
			//something wrong happened with 
			//the underlying socket report the error
			handler_(ec, bytes_transferred);
		}
	}
private:
	MutableBufferSequence buffers_;
	Handler handler_;
	void *zmq_sock_;
};


class asio_zmq_req_socket {
public:

	asio_zmq_req_socket(boost::asio::io_service& __io_service, std::string __srv_addr):
		sock_(__io_service) 
	{

		void *zmq_ctx = get_zmq_context ();
    	zmq_sock_ = zmq_socket (zmq_ctx, ZMQ_REQ);

    	int opt = 0;
    	std::size_t optlen = sizeof (opt);

    	zmq_setsockopt(zmq_sock_, ZMQ_LINGER, &opt, optlen);

    	if (zmq_sock_ == NULL) {
    		throw zmq_exception("could not create a zmq REQ socket");
    	}

    	int zfd;
    	optlen = sizeof (zfd);
    	zmq_getsockopt (zmq_sock_, ZMQ_FD, &zfd, &optlen);
    	sock_.assign (boost::asio::ip::tcp::v4(), zfd);

    	int rc = zmq_connect (zmq_sock_, __srv_addr.c_str ());
    	if (rc) {
    		throw zmq_exception("zmq socket: could not connect to " + __srv_addr);
    	}
	}

	template <typename ConstBufferSequence, typename Handler>
	void async_send (const ConstBufferSequence & buffer, Handler handler) {
		zsock_send_op<ConstBufferSequence, Handler> send_op (zmq_sock_, buffer, handler);
		sock_.async_write_some (boost::asio::null_buffers(), send_op);
	}

	template <typename MutableBufferSequence, typename Handler>
	void async_recv (const MutableBufferSequence & buffer, Handler handler) {
		zsock_recv_op<MutableBufferSequence, Handler> recv_op (zmq_sock_, buffer, handler);
		sock_.async_read_some (boost::asio::null_buffers(), recv_op);
	}

	~asio_zmq_req_socket() {
		sock_.shutdown(boost::asio::ip::tcp::socket::shutdown_both);
		sock_.close();
		zmq_close (zmq_sock_);	
	}

private:

	void *zmq_sock_ = nullptr;
	boost::asio::ip::tcp::socket sock_;
};

} //namespace asio_zmq


class my_zmq_req_client : public std::enable_shared_from_this<my_zmq_req_client>{
public:
	my_zmq_req_client(boost::asio::io_service& __io_service, const std::string& __addr):
	zsock_(__io_service, __addr)
	{	
		
	}

	void send(const std::string& __message) {
		print("my_zmq_req_client: sending message:", __message);
		zsock_.async_send (
				boost::asio::buffer(__message),
				boost::bind(&my_zmq_req_client::handle_send, shared_from_this(),
					boost::asio::placeholders::error,
					boost::asio::placeholders::bytes_transferred 
				)
		);
	}

private:

	void handle_send(const boost::system::error_code &ec, std::size_t bytes_transferred) {

		if (ec) {
			std::cout << "failed to send buffer\n";
		}
		else {
			print("my_zmq_req_client: message sent");
			zsock_.async_recv (
				boost::asio::buffer(recv_buffer_), 
				boost::bind (&my_zmq_req_client::handle_recv, shared_from_this(), boost::asio::placeholders::error,
					boost::asio::placeholders::bytes_transferred)
			);	
		}
	}

	void handle_recv(const boost::system::error_code &ec, std::size_t bytes_transferred) {

		if (!ec) {
			// if no error happened but no bytes were received because the zeromq socket 
			// is not POLL_IN ready (check the zero_mq zmq_getsockopt: it says that 
			// a socket can be reported as writable by the OS but not yet for zeromq) 
			// schedule another async recv operation. This is less likely to happen for send operations
			// and in practice never had to do it.
			if (bytes_transferred == 0)
				zsock_.async_recv (boost::asio::buffer(recv_buffer_), 
					boost::bind (&my_zmq_req_client::handle_recv, shared_from_this(), boost::asio::placeholders::error,
						boost::asio::placeholders::bytes_transferred));
			else {
				
				printf("my_zmq_req_client: zmq REP: %s\n", recv_buffer_.data());
			}
		}
	}

	asio_zmq::asio_zmq_req_socket zsock_;
	std::array<char, 256> recv_buffer_;
};


int main(int, char**) {
	
	asio_zmq::get_zmq_context();

	boost::asio::io_service io_service;
	
	std::string srv_addr("tcp://127.0.0.1:11155");
	std::string msg("zmq REQ: hello from client");

	{
		auto zclient = std::make_shared<my_zmq_req_client>(io_service, srv_addr);
		zclient->send(msg);	
	}

	
	io_service.run();

	zmq_ctx_term(asio_zmq::get_zmq_context()); 
	
}

