#ifndef TELEGRAM_SENDER_H_INCLUDED
#define TELEGRAM_SENDER_H_INCLUDED

#include <string>
#include <boost/iostreams/stream.hpp>
#define URDL_HEADER_ONLY 1
#ifdef LIBTELEGRAM_DISABLE_SSL_NO_REALLY_I_MEAN_IT_AND_I_KNOW_WHAT_IM_DOING
  #warning "SSL is disabled for outgoing messages - that is such a bad idea."
  #define URDL_DISABLE_SSL 1
  #define LIBTELEGRAM_OUTGOING_PROTO "http"
#else
  #define LIBTELEGRAM_OUTGOING_PROTO "https"
#endif // LIBTELEGRAM_DISABLE_SSL_NO_REALLY_I_MEAN_IT_AND_I_KNOW_WHAT_IM_DOING
#include <urdl/istream.hpp>
#include <json.hpp>
#include "types/user.h"

namespace telegram {

class sender {
  std::string token;                                                            // the bot's Telegram authentication token
  std::string endpoint;                                                         // the web endpoint requests are sent to
  std::string user_agent;                                                       // the user agent the bot should identify itself with

  urdl::option_set urdl_global_options;                                         // global options shared by all outgoing web requests

public:
  enum class parse_mode : char {                                                // see https://core.telegram.org/bots/api#markdown-style
    NONE,
    MARKDOWN,
    HTML,
    DEFAULT = NONE
  };
  enum class web_preview_mode : char {                                          // whether or not to allow the web preview for links, see https://core.telegram.org/bots/api#sendmessage
    DISABLE,
    ENABLE,
    DEFAULT = ENABLE
  };
  enum class notification_mode : char {                                         // whether to send the message silently, see https://core.telegram.org/bots/api#sendmessage
    DISABLE,
    ENABLE,
    DEFAULT = ENABLE
  };
  enum class chat_action_type : char {                                          // chat actions, see https://core.telegram.org/bots/api#sendchataction
    TYPING,                                                                     // typing for text messages
    UPLOAD_PHOTO,                                                               // upload_photo for photos
    RECORD_VIDEO,                                                               // record_video for videos
    UPLOAD_VIDEO,                                                               // pload_video for videos
    RECORD_AUDIO,                                                               // record_audio for audio files
    UPLOAD_AUDIO,                                                               // upload_audio for audio files
    UPLOAD_DOCUMENT,                                                            // upload_document for general files
    FIND_LOCATION                                                               // find_location for location data.
  };
  static int_fast32_t constexpr const reply_to_message_id_none = -1;
  static int_fast32_t constexpr const message_length_limit = 4096;              // see https://core.telegram.org/method/messages.sendMessage

  // TODO: add message sending stream class
  // TODO: add statistics on bytes sent and received

  sender(std::string const &token, std::string const &user_agent = "LibTelegram");

  nlohmann::json send_json(std::string const &method,
                           nlohmann::json const &tree = {},
                           unsigned int poll_timeout = 30);
  template<typename T>
  std::experimental::optional<T> send_json_and_parse(std::string const &method,
                                                     nlohmann::json const &tree = {});

  std::experimental::optional<types::user> const get_me();

  template<typename Treply_markup = types::reply_markup::force_reply>
  std::experimental::optional<types::message> send_message(int_fast64_t chat_id,
                                                           std::string const &text,
                                                           int_fast32_t reply_to_message_id = reply_to_message_id_none,
                                                           parse_mode parse = parse_mode::DEFAULT,
                                                           web_preview_mode web_preview = web_preview_mode::DEFAULT,
                                                           notification_mode notification = notification_mode::DEFAULT,
                                                           types::reply_markup::base<Treply_markup> *reply_markup = nullptr);
  std::experimental::optional<types::message> send_message(std::string channel_name,
                                                           std::string const &text,
                                                           int_fast32_t reply_to_message_id = reply_to_message_id_none,
                                                           parse_mode parse = parse_mode::DEFAULT,
                                                           web_preview_mode web_preview = web_preview_mode::DEFAULT,
                                                           notification_mode notification = notification_mode::DEFAULT);

  std::experimental::optional<types::message> forward_message(int_fast64_t chat_id,
                                                              int_fast64_t from_chat_id,
                                                              int_fast32_t message_id,
                                                              notification_mode notification = notification_mode::DEFAULT);

  bool send_chat_action(int_fast64_t chat_id, chat_action_type action = chat_action_type::TYPING);

  std::experimental::optional<types::file> get_file(std::string const &file_id);
};

sender::sender(std::string const &this_token,
               std::string const &this_user_agent) try
  : token(this_token),
    endpoint(LIBTELEGRAM_OUTGOING_PROTO "://api.telegram.org/bot" + this_token + "/"),
    user_agent(this_user_agent) {
  /// Construct a sender with the given token
  urdl_global_options.set_option(urdl::http::max_redirects(0));
  urdl_global_options.set_option(urdl::http::user_agent(user_agent));
  urdl_global_options.set_option(urdl::http::request_method("POST"));
} catch(std::exception const &e) {
  std::cerr << "LibTelegram: Sender: Exception during construction: " << e.what() << std::endl;
}

nlohmann::json sender::send_json(std::string const &method,
                                 nlohmann::json const &tree,
                                 unsigned int poll_timeout) {
  /// Function to send a json tree to the given method and get back a response, useful if you want to send custom requests
  std::cerr << "LibTelegram: Sender: DEBUG: json request:" << std::endl;
  std::cerr << tree.dump(2) << std::endl;

  urdl::istream stream;
  stream.set_options(urdl_global_options);                                      // apply the global options to this stream
  stream.set_option(urdl::http::request_content_type("application/json"));
  stream.set_option(urdl::http::request_content(tree.dump()));                  // write the json dump as the request body
  unsigned int const timeout_ms = poll_timeout * 1000;                          // the stream expects timeouts in milliseconds
  stream.open_timeout(timeout_ms);
  stream.read_timeout(timeout_ms);
  urdl::url const url(endpoint + method);
  stream.open(url);
  if(!stream) {
    std::cerr << "LibTelegram: Sender: Unable to open URL " << url.to_string() << ": " << stream.error().message() << ", headers: " << stream.headers() << std::endl;
    return nlohmann::json();                                                    // return an empty tree
  }

  std::string reply;
  {
    std::string reply_line;
    while(std::getline(stream, reply_line)) {
      reply += reply_line + '\n';                                               // concatenate all lines of input
    }
    reply += reply_line;                                                        // input is not newline-terminated, so don't forget the last line
  }
  if(reply.empty()) {
    std::cerr << "LibTelegram: Sender: Received empty reply to send_json" << std::endl;
    return nlohmann::json();                                                    // return an empty tree
  }
  boost::iostreams::stream<boost::iostreams::array_source> reply_stream(reply.data(), reply.size());
  nlohmann::json reply_tree;                                                    // property tree to contain the reply
  try {
    reply_stream >> reply_tree;
  } catch(std::exception &e) {
    std::cerr << "LibTelegram: Sender: Received unparseable JSON in reply to send_json: " << e.what() << std::endl;
  }
  return reply_tree;
}

template<typename T>
std::experimental::optional<T> sender::send_json_and_parse(std::string const &method,
                                                           nlohmann::json const &tree) {
  /// Wrapper function to send a json object and get back a complete object of the specified template typen
  auto reply_tree(send_json(method, tree));
  std::cerr << "LibTelegram: Sender: DEBUG: json to send:" << std::endl;
  std::cerr << tree.dump(2) << std::endl;
  if(reply_tree["ok"] != true) {
    std::cerr << "LibTelegram: Sender: Returned status other than OK in reply to " << method << " trying to get " << typeid(T).name() << ":" << std::endl;
    std::cerr << reply_tree.dump(2) << std::endl;
    return std::experimental::nullopt;
  }
  try {
    return types::make_optional<T>(reply_tree, "result");
  } catch(std::exception &e) {
    std::cerr << "LibTelegram: Sender: Exception parsing the following tree to extract a " << typeid(T).name() << ": " << e.what() << std::endl;
    std::cerr << reply_tree.dump(2) << std::endl;
    return std::experimental::nullopt;
  }
}

std::experimental::optional<types::user> const sender::get_me() {
  /// Send a getme request - see https://core.telegram.org/bots/api#getme
  return send_json_and_parse<types::user>("getMe");
}

template<typename Treply_markup>
std::experimental::optional<types::message> sender::send_message(int_fast64_t chat_id,
                                                                 std::string const &text,
                                                                 int_fast32_t reply_to_message_id,
                                                                 parse_mode parse,
                                                                 web_preview_mode web_preview,
                                                                 notification_mode notification,
                                                                 types::reply_markup::base<Treply_markup> *reply_markup) {
  /// Send a message to a chat id - see https://core.telegram.org/bots/api#sendmessage
  if(text.empty()) {
    return std::experimental::nullopt;                                          // don't attempt to send empty messages - this would be an error
  }
  if(text.size() > message_length_limit) {                                      // recursively split this message if it's too long
    send_message(chat_id,
                 text.substr(0, message_length_limit),                          // send just the first allowed number of characters in the first half
                 reply_to_message_id,
                 parse,
                 web_preview,
                 notification,
                 reply_markup);
    return send_message(chat_id,
                        text.substr(message_length_limit, std::string::npos),   // send the remaining characters - this will subdivide again recursively if need be
                        reply_to_message_id,
                        parse,
                        web_preview,
                        notification,
                        reply_markup);                                          // note - we disregard return messages from any except the last
  }
  std::cerr << "DEBUG: sending message \"" << text << "\" to chat id " << chat_id << std::endl;
  nlohmann::json tree;                                                          // a json container object for our data
  tree["chat_id"] = chat_id;
  tree["text"] = text;
  if(parse != parse_mode::DEFAULT) {                                            // don't waste bandwidth sending the default option
    switch(parse) {
    case parse_mode::NONE:
      break;
    case parse_mode::MARKDOWN:
      tree["parse_mode"] = "Markdown";
      break;
    case parse_mode::HTML:
      tree["parse_mode"] = "HTML";
      break;
    }
  }
  if(web_preview != web_preview_mode::DEFAULT) {                                // don't waste bandwidth sending the default option
    switch(web_preview) {
    case web_preview_mode::DISABLE:
      tree["disable_web_page_preview"] = true;
      break;
    case web_preview_mode::ENABLE:
      tree["disable_web_page_preview"] = false;
      break;
    }
  }
  if(notification != notification_mode::DEFAULT) {                              // don't waste bandwidth sending the default option
    switch(notification) {
    case notification_mode::DISABLE:
      tree["disable_notification"] = true;
      break;
    case notification_mode::ENABLE:
      tree["disable_notification"] = false;
      break;
    }
  }
  if(reply_to_message_id != reply_to_message_id_none) {
    tree["reply_to_message_id"] = reply_to_message_id;
  }
  if(reply_markup) {
    reply_markup->get(tree);
  }
  return send_json_and_parse<types::message>("sendMessage", tree);
}
std::experimental::optional<types::message> sender::send_message(std::string channel_name,
                                                                 std::string const &text,
                                                                 int_fast32_t reply_to_message_id,
                                                                 parse_mode parse,
                                                                 web_preview_mode web_preview,
                                                                 notification_mode notification) {
  /// Send a message to a channel name - see https://core.telegram.org/bots/api#sendmessage
  if(text.empty()) {
    return std::experimental::nullopt;                                          // don't attempt to send empty messages - this would be an error
  }
  if(text.size() > message_length_limit) {                                      // recursively split this message if it's too long
    send_message(channel_name,
                 text.substr(0, message_length_limit),                          // send just the first allowed number of characters in the first half
                 reply_to_message_id,
                 parse,
                 web_preview,
                 notification);
    return send_message(channel_name,
                        text.substr(message_length_limit, std::string::npos),   // send the remaining characters - this will subdivide again recursively if need be
                        reply_to_message_id,
                        parse,
                        web_preview,
                        notification);                                          // note - we disregard return messages from any except the last
  }
  std::cerr << "DEBUG: sending message \"" << text << "\" to channel name " << channel_name << std::endl;
  nlohmann::json tree;                                                          // a json container object for our data
  tree["chat_id"] = channel_name;
  tree["text"] = text;
  if(parse != parse_mode::DEFAULT) {                                            // don't waste bandwidth sending the default option
    switch(parse) {
    case parse_mode::NONE:
      break;
    case parse_mode::MARKDOWN:
      tree["parse_mode"] = "Markdown";
      break;
    case parse_mode::HTML:
      tree["parse_mode"] = "HTML";
      break;
    }
  }
  if(web_preview != web_preview_mode::DEFAULT) {                                // don't waste bandwidth sending the default option
    switch(web_preview) {
    case web_preview_mode::DISABLE:
      tree["disable_web_page_preview"] = true;
      break;
    case web_preview_mode::ENABLE:
      tree["disable_web_page_preview"] = false;
      break;
    }
  }
  if(notification != notification_mode::DEFAULT) {                              // don't waste bandwidth sending the default option
    switch(notification) {
    case notification_mode::DISABLE:
      tree["disable_notification"] = true;
      break;
    case notification_mode::ENABLE:
      tree["disable_notification"] = false;
      break;
    }
  }
  if(reply_to_message_id != reply_to_message_id_none) {
    tree["reply_to_message_id"] = reply_to_message_id;
  }
  return send_json_and_parse<types::message>("sendMessage", tree);
}

std::experimental::optional<types::message> sender::forward_message(int_fast64_t chat_id,
                                                                    int_fast64_t from_chat_id,
                                                                    int_fast32_t message_id,
                                                                    notification_mode notification) {
  /// Forward a message to a chat id - see https://core.telegram.org/bots/api#forwardmessage
  std::cerr << "DEBUG: forwarding message " << message_id << " from chat " << from_chat_id << " to chat id " << chat_id << std::endl;
  nlohmann::json tree;                                                          // json object to put our data into
  tree["chat_id"]      = chat_id;
  tree["from_chat_id"] = from_chat_id;
  tree["message_id"]   = message_id;
  if(notification != notification_mode::DEFAULT) {                              // don't waste bandwidth sending the default option
    switch(notification) {
    case notification_mode::DISABLE:
      tree["disable_notification"] = true;
      break;
    case notification_mode::ENABLE:
      tree["disable_notification"] = false;
      break;
    }
  }
  return send_json_and_parse<types::message>("forwardMessage", tree);
}

bool sender::send_chat_action(int_fast64_t chat_id,
                              chat_action_type action) {
  /// Send a chat action - see https://core.telegram.org/bots/api#sendchataction
  /// Return is whether it succeeded
  nlohmann::json tree;
  tree["chat_id"] = chat_id;
  switch(action) {
  case chat_action_type::TYPING:
    tree["action"] = "typing";
    break;
  case chat_action_type::UPLOAD_PHOTO:
    tree["action"] = "upload_photo";
    break;
  case chat_action_type::RECORD_VIDEO:
    tree["action"] = "record_video";
    break;
  case chat_action_type::UPLOAD_VIDEO:
    tree["action"] = "pload_video";
    break;
  case chat_action_type::RECORD_AUDIO:
    tree["action"] = "record_audio";
    break;
  case chat_action_type::UPLOAD_AUDIO:
    tree["action"] = "upload_audio";
    break;
  case chat_action_type::UPLOAD_DOCUMENT:
    tree["action"] = "upload_document";
    break;
  case chat_action_type::FIND_LOCATION:
    tree["action"] = "find_location";
    break;
  }
  auto reply_tree(send_json("sendChatAction", tree));
  std::cerr << tree.dump(2) << std::endl;
  if(reply_tree["ok"] != true) {
    std::cerr << "LibTelegram: Sender: Returned status other than OK in reply to sendChatAction expecting a bool:" << std::endl;
    std::cerr << reply_tree.dump(2) << std::endl;
    return false;
  }
  return reply_tree.at("result");
}

std::experimental::optional<types::file> sender::get_file(std::string const &file_id) {
  /// Get download info about a file (as a file object) - see https://core.telegram.org/bots/api#getfile
  nlohmann::json tree;
  tree["file_id"] = file_id;
  return send_json_and_parse<types::file>("getFile", tree);
}

}

#endif // TELEGRAM_SENDER_H_INCLUDED
