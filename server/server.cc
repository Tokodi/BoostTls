#include "server.h"
#include "connection.h"

#include <exception>
#include <iostream>
#include <memory>

server::server(const std::uint16_t port)
    : _port(port)
    , _acceptor(_ioContext, boost::asio::ip::tcp::endpoint(boost::asio::ip::address_v6::any(), port))
    , _sslContext(boost::asio::ssl::context::tlsv12_server) {
        initializeTls();
}

void server::start() {
    std::cout << "[Server] Starting server" << std::endl;
    accept();
    _ioContextThread = std::thread([this]() { _ioContext.run(); });
}

void server::stop() {
    std::cout << "[Server] Terminating server" << std::endl;
    if (!_ioContext.stopped())
        _ioContext.stop();

    if (_ioContextThread.joinable())
        _ioContextThread.join();
}

void server::initializeTls() try {
    boost::system::error_code error;

    _sslContext.set_options(boost::asio::ssl::context::default_workarounds, error);
    if (error) {
        throw std::runtime_error("Could not set SSL context options (" + error.message() + ")");
    }

    _sslContext.set_password_callback(std::bind(&server::getPassword, this), error);
    if (error) {
        throw std::runtime_error("Could not set password callback (" + error.message() + ")");
    }

    _sslContext.use_certificate_file("../certs/server.crt", boost::asio::ssl::context::pem, error);
    if (error) {
        throw std::runtime_error("Could not set certificate file (" + error.message() + ")");
    }

    _sslContext.use_private_key_file("../certs/server.key", boost::asio::ssl::context::pem, error);
    if (error) {
        throw std::runtime_error("Could not set private key file (" + error.message() + ")");
    }

    _sslContext.load_verify_file("../certs/ca.pem", error);
    if (error) {
        throw std::runtime_error("Could not load CA certificate file (" + error.message() + ")");
    }

    _sslContext.set_verify_mode(boost::asio::ssl::verify_peer | boost::asio::ssl::verify_fail_if_no_peer_cert, error);
    if (error) {
        throw std::runtime_error("Could not set verify mode (" + error.message() + ")");
    }

    _sslContext.set_verify_callback(std::bind(&server::certVerifyCB, this, std::placeholders::_1, std::placeholders::_2), error);
    if (error) {
        throw std::runtime_error("Could not set verify callback function (" + error.message() + ")");
    }
} catch (const std::runtime_error& ex) {
    std::cout << "[Server] TLS initialization error: " << ex.what() << std::endl;
    throw std::runtime_error("TLS initialization error");
}

void server::accept() {
    std::cout << "[Server] Waiting for connections..." << std::endl;
    _acceptor.async_accept(
        [this](const boost::system::error_code& error, boost::asio::ip::tcp::socket socket) {
            if (!error) {
                std::cout << "[Server] Client connected (" << socket.remote_endpoint() << ")" << std::endl;
                //socket.non_blocking(true); // NOTE: If set here, handshake fails with "resource temporarly unavailable"
                std::make_shared<connection>(std::move(socket), _sslContext)->start();
            }
            accept();
        }
    );
}

std::string server::getPassword() const {
    return "serverKeyPass";
}

bool server::certVerifyCB(bool preverified, boost::asio::ssl::verify_context& ctx) {
    // The verify callback can be used to check whether the certificate that is
    // being presented is valid for the peer. For example, RFC 2818 describes
    // the steps involved in doing this for HTTPS. Consult the OpenSSL
    // documentation for more details. Note that the callback is called once
    // for each certificate in the certificate chain, starting from the root
    // certificate authority.

    char subjectName[256];
    X509* cert = X509_STORE_CTX_get_current_cert(ctx.native_handle());
    X509_NAME_oneline(X509_get_subject_name(cert), subjectName, 256);
    std::cout << "[Server] Verifying " << subjectName << std::endl;
    if (preverified)
        std::cout << "[Server] Verified!" << std::endl;
    else
        std::cout << "[Server] Verification failed!" << std::endl;

    return preverified;
}
