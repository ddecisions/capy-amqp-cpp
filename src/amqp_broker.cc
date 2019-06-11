//
// Created by denn nevera on 2019-05-31.
//

#include "capy/amqp_broker.h"
#include "amqp.h"
#include "amqp_tcp_socket.h"

#include "capy/dispatchq.h"
#include <assert.h>
#include <atomic>
#include <thread>
#include <future>

namespace capy::amqp {

    bool die_on_amqp_error(amqp_rpc_reply_t x, char const *context, std::string &return_string);

    class BrokerImpl {

    public:
        amqp_channel_t channel_id;
        std::string exchange_name;
        bool isExited;
        static std::atomic_uint32_t correlation_id;
        amqp_connection_state_t producer_conn;

    public:

        BrokerImpl(amqp_channel_t aChannel_id):isExited(false),channel_id(aChannel_id){};

        ~BrokerImpl(){

          isExited = true;

          std::string error_message;

          ///
          /// @todo
          /// Solve debug logging problem...
          ///

          if (die_on_amqp_error(amqp_channel_close(producer_conn, channel_id, AMQP_REPLY_SUCCESS), "Closing channel", error_message)) {
            std::cerr << error_message << std::endl;
          }

          if (die_on_amqp_error(amqp_connection_close(producer_conn, AMQP_REPLY_SUCCESS), "Closing connection", error_message)) {
            std::cerr << error_message << std::endl;
          }

          if(amqp_destroy_connection(producer_conn)){
            std::cerr << "Ending connection error..." << std::endl;
          }

        }


        void consume_once(const std::function<void(const capy::json& body,
                                                   const std::string& reply_to,
                                                   uint64_t delivery_tag)> on_replay,
                          const std::function<void()> on_complete) {

          amqp_rpc_reply_t ret;
          amqp_envelope_t envelope;

          ret = amqp_consume_message(producer_conn, &envelope, NULL, 0);

          if (AMQP_RESPONSE_NORMAL == ret.reply_type) {

            std::string reply_to_routing_key;

            if (envelope.message.properties._flags & AMQP_BASIC_REPLY_TO_FLAG) {

              reply_to_routing_key = std::string(
                      static_cast<char *>(envelope.message.properties.reply_to.bytes),
                      envelope.message.properties.reply_to.len);
            }

            std::vector<std::uint8_t> buffer(
                    static_cast<std::uint8_t *>(envelope.message.body.bytes),
                    static_cast<std::uint8_t *>(envelope.message.body.bytes) + envelope.message.body.len);

            try {
              capy::json json = json::from_msgpack(buffer);
              on_replay(json, reply_to_routing_key, envelope.delivery_tag);

            }
            catch (std::exception &exception) {
              amqp_queue_delete(producer_conn, channel_id, amqp_cstring_bytes(reply_to_routing_key.c_str()), 1, 1);
              std::cerr << "capy::Broker::error: consume message: " << exception.what() << std::endl;
            }

          }

          amqp_destroy_envelope(&envelope);
          amqp_maybe_release_buffers(producer_conn);
          amqp_maybe_release_buffers_on_channel(producer_conn, channel_id);

          on_complete();
        }

        void consuming(const std::function<void(const capy::json& body,
                                                const std::string& reply_to,
                                                uint64_t delivery_tag)> on_replay) {

          while (!isExited) {
            consume_once(on_replay,[]{});
          }

        }


        Error publish(const capy::json& message,
                      const std::string &exchange_name,
                      const std::string& routing_key,
                      const std::optional<ListenHandler>& on_data) {

          amqp_basic_properties_t props;
          std::string reply_to_queue;

          uint32_t current_id = 0;

          if (on_data.has_value()) {

            current_id = BrokerImpl::correlation_id++;

            //
            // create private reply_to queue
            //

            amqp_queue_declare_ok_t *r =
                    amqp_queue_declare(
                            producer_conn, channel_id, amqp_empty_bytes, 0, 0, 1, 1, amqp_empty_table);

            std::string error_message;

            if (!r) {
              return capy::Error(BrokerError::QUEUE_DECLARATION, "Connection has been lost");
            }

            if (die_on_amqp_error(amqp_get_rpc_reply(producer_conn), "Publisher declaring queue", error_message)) {
              return capy::Error(BrokerError::QUEUE_DECLARATION, error_message);
            }

            if (r->queue.bytes == NULL) {
              return capy::Error(BrokerError::QUEUE_DECLARATION, "Out of memory while copying queue name");
            }

            reply_to_queue = std::string(static_cast<char *>(r->queue.bytes), r->queue.len);

            props._flags = AMQP_BASIC_CONTENT_TYPE_FLAG |
                           AMQP_BASIC_DELIVERY_MODE_FLAG | AMQP_BASIC_REPLY_TO_FLAG |
                           AMQP_BASIC_CORRELATION_ID_FLAG;

            props.content_type = amqp_cstring_bytes("text/plain");
            props.delivery_mode = 2; /* persistent delivery mode */
            props.reply_to = amqp_cstring_bytes(reply_to_queue.c_str());

            if (props.reply_to.bytes == NULL) {
              return capy::Error(BrokerError::QUEUE_DECLARATION, "Out of memory while copying queue name");
            }

            props.correlation_id = amqp_cstring_bytes(std::to_string(current_id).c_str());

          }

          //
          // publish
          //

          amqp_bytes_t message_bytes;
          auto data = json::to_msgpack(message);

          message_bytes.len = data.size();
          message_bytes.bytes = data.data();


          auto ret = amqp_basic_publish(producer_conn,
                                        channel_id,
                                        exchange_name.empty() ? amqp_empty_bytes : amqp_cstring_bytes(exchange_name.c_str()),
                                        amqp_cstring_bytes(routing_key.c_str()),
                                        0,
                                        0,
                                        on_data.has_value() ? &props : NULL,
                                        message_bytes);


          if (ret) {
            return Error(BrokerError::PUBLISH, error_string("Could not produce message to routingkey: %s", routing_key.c_str()));
          }

          if (on_data.has_value()) {

            std::string error_message;


            //
            // Consume queue
            //
            amqp_basic_consume(producer_conn,
                               channel_id,
                               amqp_cstring_bytes(reply_to_queue.c_str()),
                               amqp_empty_bytes,
                               0, 0, 0, amqp_empty_table);

            if (die_on_amqp_error(amqp_get_rpc_reply(producer_conn), "Consuming rpc", error_message)) {
              return capy::Error(BrokerError::QUEUE_CONSUMING, error_message);
            }


            consume_once([&on_data, this, current_id, &props](const capy::json& body, const std::string& reply_to, uint64_t delivery_tag) {

              capy::amqp::Task::Instance().async([=]{
                  Result <json> skip;
                  on_data.value()(body,skip);
              });

            }, [this, &props]{
                amqp_queue_delete(producer_conn, channel_id, props.reply_to, 0, 0);
            });

          }

          return Error(CommonError::OK);
        }

        void listen(
                const std::string& queue,
                const std::vector<std::string>& routing_keys,
                const capy::amqp::ListenHandler& on_data) {
          try {

            std::string error_message;

            //
            //
            // declare named queue
            //

            amqp_queue_declare_ok_t *declare =
                    amqp_queue_declare(producer_conn, channel_id, amqp_cstring_bytes(queue.c_str()), 0, 1, 0, 0, amqp_empty_table);


            if (die_on_amqp_error(amqp_get_rpc_reply(producer_conn), "Listener declaring queue", error_message)) {
              Result<json> replay;
              return on_data(
                      capy::make_unexpected(capy::Error(BrokerError::QUEUE_DECLARATION, error_message)),
                      replay);
            }

            if (declare->queue.bytes == NULL) {
              Result<json> replay;
              return on_data(
                      capy::make_unexpected(
                              capy::Error(BrokerError::QUEUE_DECLARATION, "Queue name declaration mismatched")),
                      replay);
            }

            //
            // bind routing key
            //

            for (auto &routing_key: routing_keys) {

              amqp_queue_bind(producer_conn,
                              channel_id,
                              amqp_cstring_bytes(queue.c_str()),
                              amqp_cstring_bytes(exchange_name.c_str()),
                              amqp_cstring_bytes(routing_key.c_str()), amqp_empty_table);

              if (die_on_amqp_error(amqp_get_rpc_reply(producer_conn), "Binding queue", error_message)) {
                Result<json> replay;
                return on_data(
                        capy::make_unexpected(capy::Error(BrokerError::QUEUE_DECLARATION, error_message)),
                        replay);
              }
            }

            //
            // Consume queue
            //
            amqp_basic_consume(producer_conn,
                               channel_id,
                               amqp_cstring_bytes(queue.c_str()),
                               amqp_empty_bytes,
                               0, 0, 0, amqp_empty_table);

            if (die_on_amqp_error(amqp_get_rpc_reply(producer_conn), "Consuming", error_message)) {
              Result<json> replay;
              return on_data(
                      capy::make_unexpected(capy::Error(BrokerError::QUEUE_CONSUMING, error_message)),
                      replay);
            }

            consuming([&on_data, this](const capy::json& body, const std::string& reply_to, uint64_t delivery_tag){

                amqp_basic_ack(producer_conn, channel_id, delivery_tag, 1);

                capy::amqp::Task::Instance().async([=]{

                    try {

                      Result<json> replay;

                      on_data(body, replay);

                      if (!replay) {
                        return;
                      }

                      if (replay->empty()) {
                        std::cerr << "capy::Broker::error: replay is empty" << std::endl;
                        return;
                      }

                      publish(replay.value(), "", reply_to, std::nullopt);

                    }
                    catch (json::exception &exception) {
                      throw_abort(exception.what());
                    }
                    catch (...) {
                      throw_abort("Unexpected excaption...");
                    }
                });
            });

          }
          catch (std::exception &exception) {
            std::cerr << "capy::Broker::error: " << exception.what() << std::endl;
          }
          catch (...) {
            std::cerr << "capy::Broker::error: Unknown error" << std::endl;
          }
        }

    };

    std::atomic_uint32_t BrokerImpl::correlation_id = 0;


    //
    // MARK: - bind
    //
    Result<Broker> Broker::Bind(const capy::amqp::Address& address, const std::string& exchange_name) {

      amqp_channel_t channel_id = 1;

      amqp_connection_state_t producer_conn = amqp_new_connection();
      amqp_socket_t *producer_socket = amqp_tcp_socket_new(producer_conn);

      if (!producer_socket) {
        return capy::make_unexpected(capy::Error(BrokerError::CONNECTION,
                                                 error_string("Tcp socket could not be created...")));
      }

      if (amqp_socket_open(producer_socket, address.get_hostname().c_str(), static_cast<int>(address.get_port()))) {
        amqp_destroy_connection(producer_conn);
        return capy::make_unexpected(capy::Error(BrokerError::CONNECTION,
                                                 error_string("Tcp socket could not be opened for: %s:%i",
                                                              address.get_hostname().c_str(),
                                                              address.get_port())));
      }

      amqp_rpc_reply_t ret = amqp_login(producer_conn, address.get_vhost().c_str(),
                                        AMQP_DEFAULT_MAX_CHANNELS,
                                        AMQP_DEFAULT_FRAME_SIZE,
                                        0,
                                        AMQP_SASL_METHOD_PLAIN,
                                        address.get_login().get_username().c_str(),
                                        address.get_login().get_password().c_str());


      std::string error_message;

      if (die_on_amqp_error(ret, "Producer login", error_message)) {
        amqp_connection_close(producer_conn, AMQP_REPLY_SUCCESS);
        amqp_destroy_connection(producer_conn);
        return capy::make_unexpected(capy::Error(BrokerError::LOGIN, error_message));
      }

      amqp_channel_open(producer_conn, channel_id);

      if (die_on_amqp_error(amqp_get_rpc_reply(producer_conn), "Opening producer channel", error_message)) {
        amqp_connection_close(producer_conn, AMQP_REPLY_SUCCESS);
        amqp_destroy_connection(producer_conn);
        return capy::make_unexpected(capy::Error(BrokerError::CHANNEL, error_message));
      }

      auto impl = std::shared_ptr<BrokerImpl>(new BrokerImpl(channel_id));

      amqp_basic_qos(producer_conn,channel_id,0,1,0);

      impl->producer_conn = producer_conn;
      impl->exchange_name = exchange_name;

      return Broker(impl);

    }

    //
    // Broker constructors
    //
    Broker::Broker(const std::shared_ptr<capy::amqp::BrokerImpl> &impl) : impl_(impl) {}
    Broker::Broker() : impl_(nullptr) {}


    //
    // fetch
    //
    Error Broker::fetch(const capy::json& message, const std::string& routing_key, const FetchHandler& on_data) {
      return impl_->publish(message, impl_->exchange_name, routing_key, [&](const Result<json> &message, Result<json> &replay){
          try {
            on_data(message);
          }
          catch (json::exception &exception) {
            throw_abort(exception.what());
          }
          catch (...) {
            throw_abort("Unexpected excaption...");
          }
      });
    }

    //
    // listen
    //
    void Broker::listen(
            const std::string& queue,
            const std::vector<std::string>& routing_keys,
            const capy::amqp::ListenHandler& on_data) {
      return impl_->listen(queue, routing_keys, on_data);
    }

    //
    // publish
    //
    Error Broker::publish(const capy::json& message, const std::string& routing_key) {
      return impl_->publish(message, impl_->exchange_name,routing_key,std::nullopt);
    }

    ///
    /// Errors...
    ///

    std::string BrokerErrorCategory::message(int ev) const {
      switch (ev) {
        case static_cast<int>(BrokerError::CONNECTION):
          return "Connection error";
        default:
          return ErrorCategory::message(ev);
      }
    }

    const std::error_category &broker_error_category() {
      static BrokerErrorCategory instance;
      return instance;
    }

    std::error_condition make_error_condition(capy::amqp::BrokerError e) {
      return std::error_condition(
              static_cast<int>(e),
              capy::amqp::broker_error_category());
    }

    bool die_on_amqp_error(amqp_rpc_reply_t x, char const *context, std::string &return_string) {

      switch (x.reply_type) {

        case AMQP_RESPONSE_NORMAL:
          return false;

        case AMQP_RESPONSE_NONE:
          return_string = error_string("%s: missing RPC reply type!\n", context);
          break;

        case AMQP_RESPONSE_LIBRARY_EXCEPTION:
          return_string = error_string("%s: %s\n", context, amqp_error_string2(x.library_error));
          break;

        case AMQP_RESPONSE_SERVER_EXCEPTION:
          switch (x.reply.id) {
            case AMQP_CONNECTION_CLOSE_METHOD: {
              amqp_connection_close_t *m = (amqp_connection_close_t *) x.reply.decoded;

              return_string = error_string("%s: server connection error %uh, message: %.*s\n",
                                           context, m->reply_code, (int) m->reply_text.len,
                                           (char *) m->reply_text.bytes);
            }
              break;
            case AMQP_CHANNEL_CLOSE_METHOD: {
              amqp_channel_close_t *m = (amqp_channel_close_t *) x.reply.decoded;
              return_string = error_string("%s: server channel error %uh, message: %.*s\n",
                                           context, m->reply_code, (int) m->reply_text.len,
                                           (char *) m->reply_text.bytes);
            }
              break;
            default:
              return_string = error_string("%s: unknown server error, method id 0x%08X\n",
                                           context, x.reply.id);
          }
      }

      return true;
    }
}

