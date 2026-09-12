#include "TelegramBot.hpp"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <utility>

using json=nlohmann::json;
namespace {
size_t writeCallback(void* contents,size_t size,size_t nmemb,void* userp){const size_t total=size*nmemb;static_cast<std::string*>(userp)->append(static_cast<char*>(contents),total);return total;}
}

TelegramBot::TelegramBot(std::string token,int pollTimeout,bool privateChatsOnly):token_(std::move(token)),pollTimeout_(std::max(0,pollTimeout)),privateChatsOnly_(privateChatsOnly){
    if(token_.empty())throw std::invalid_argument("Telegram token is empty");
    if(curl_global_init(CURL_GLOBAL_DEFAULT)!=CURLE_OK)throw std::runtime_error("Unable to initialize libcurl");
}
TelegramBot::~TelegramBot(){stop();if(periodicThread_.joinable())periodicThread_.join();curl_global_cleanup();}
void TelegramBot::setMessageHandler(MessageHandler handler){handler_=std::move(handler);}
void TelegramBot::setCallbackHandler(CallbackHandler handler){callbackHandler_=std::move(handler);}
void TelegramBot::setStopChecker(std::function<bool()> checker){stopChecker_=std::move(checker);}
void TelegramBot::setPeriodicHandler(PeriodicHandler handler,int intervalSeconds){periodicHandler_=std::move(handler);periodicIntervalSeconds_=std::max(5,intervalSeconds);}
void TelegramBot::stop(){running_=false;}
bool TelegramBot::stopRequested()const{return !running_||(stopChecker_&&stopChecker_());}
std::string TelegramBot::apiUrl(const std::string& method)const{return "https://api.telegram.org/bot"+token_+"/"+method;}

std::string TelegramBot::urlEncode(const std::string& value){
    CURL* curl=curl_easy_init();if(!curl)throw std::runtime_error("Unable to initialize CURL");
    char* encoded=curl_easy_escape(curl,value.c_str(),static_cast<int>(value.size()));
    if(!encoded){curl_easy_cleanup(curl);throw std::runtime_error("Unable to URL encode value");}
    std::string result(encoded);curl_free(encoded);curl_easy_cleanup(curl);return result;
}

TelegramBot::HttpResponse TelegramBot::postForm(const std::string& method,const std::string& form){
    CURL* curl=curl_easy_init();if(!curl)return{0,"",true};HttpResponse response;const std::string url=apiUrl(method);
    curl_easy_setopt(curl,CURLOPT_URL,url.c_str());curl_easy_setopt(curl,CURLOPT_POST,1L);curl_easy_setopt(curl,CURLOPT_POSTFIELDS,form.c_str());curl_easy_setopt(curl,CURLOPT_POSTFIELDSIZE,static_cast<long>(form.size()));
    curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,writeCallback);curl_easy_setopt(curl,CURLOPT_WRITEDATA,&response.body);curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT,15L);curl_easy_setopt(curl,CURLOPT_TIMEOUT,static_cast<long>(std::max(30,pollTimeout_+20)));curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,1L);curl_easy_setopt(curl,CURLOPT_NOSIGNAL,1L);
    const CURLcode result=curl_easy_perform(curl);if(result!=CURLE_OK){response.networkError=true;std::cerr<<"Telegram network error: "<<curl_easy_strerror(result)<<'\n';}
    curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&response.httpCode);curl_easy_cleanup(curl);return response;
}

bool TelegramBot::deleteWebhook(){const auto response=postForm("deleteWebhook","");if(response.networkError)return false;try{return json::parse(response.body).value("ok",false);}catch(...){return false;}}
bool TelegramBot::getMe(std::string& username){const auto response=postForm("getMe","");if(response.networkError)return false;try{const auto data=json::parse(response.body);if(!data.value("ok",false))return false;username=data.value("result",json::object()).value("username","");return true;}catch(...){return false;}}
std::string TelegramBot::limitUtf8(const std::string& text,std::size_t maxBytes){if(text.size()<=maxBytes)return text;size_t p=maxBytes;while(p>0&&(static_cast<unsigned char>(text[p])&0xC0)==0x80)--p;return text.substr(0,p);}
std::string TelegramBot::makeKeyboardJson(const std::vector<std::vector<std::pair<std::string,std::string>>>& buttons){json keyboard=json::array();for(const auto& row:buttons){json r=json::array();for(const auto& button:row)r.push_back({{"text",button.first},{"callback_data",button.second}});keyboard.push_back(std::move(r));}return json({{"inline_keyboard",keyboard}}).dump();}

SendStatus TelegramBot::sendSingleMessage(long long chatId,const std::string& text,const std::string& replyMarkup){
    std::lock_guard<std::mutex> lock(sendMutex_);const std::string safeText=limitUtf8(text,4096);const auto now=std::chrono::steady_clock::now();constexpr auto interval=std::chrono::milliseconds(350);
    const auto it=lastSend_.find(chatId);if(it!=lastSend_.end()){const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(now-it->second);if(elapsed<interval)std::this_thread::sleep_for(interval-elapsed);}
    std::string form="chat_id="+urlEncode(std::to_string(chatId))+"&text="+urlEncode(safeText);if(!replyMarkup.empty())form+="&reply_markup="+urlEncode(replyMarkup);
    const auto response=postForm("sendMessage",form);if(response.networkError)return SendStatus::TemporaryFailure;
    try{const auto data=json::parse(response.body);if(data.value("ok",false)){lastSend_[chatId]=std::chrono::steady_clock::now();return SendStatus::Success;}const int error=data.value("error_code",0);return error==429||error>=500?SendStatus::TemporaryFailure:SendStatus::PermanentFailure;}catch(...){return SendStatus::TemporaryFailure;}
}
SendStatus TelegramBot::sendMessage(long long chatId,const std::string& text){return text.empty()?SendStatus::PermanentFailure:sendSingleMessage(chatId,text);}
SendStatus TelegramBot::sendMessageWithKeyboard(long long chatId,const std::string& text,const std::vector<std::vector<std::pair<std::string,std::string>>>& buttons){return text.empty()?SendStatus::PermanentFailure:sendSingleMessage(chatId,text,makeKeyboardJson(buttons));}
bool TelegramBot::answerCallbackQuery(const std::string& id){if(id.empty())return false;const auto response=postForm("answerCallbackQuery","callback_query_id="+urlEncode(id));if(response.networkError)return false;try{return json::parse(response.body).value("ok",false);}catch(...){return false;}}

void TelegramBot::periodicLoop(){
    while(!stopRequested()){
        const auto started=std::chrono::steady_clock::now();
        try{if(periodicHandler_)periodicHandler_();}catch(const std::exception& error){std::cerr<<"Periodic handler error: "<<error.what()<<'\n';}catch(...){std::cerr<<"Periodic handler error: unknown exception\n";}
        const auto next=started+std::chrono::seconds(periodicIntervalSeconds_);
        while(!stopRequested()&&std::chrono::steady_clock::now()<next)std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

void TelegramBot::run(){
    std::string botUsername;if(!deleteWebhook())std::cerr<<"Warning: unable to delete Telegram webhook.\n";if(!getMe(botUsername))throw std::runtime_error("Telegram getMe failed. Check TG_BOT_TOKEN.");
    std::cout<<"Telegram bot started"<<(botUsername.empty()?"":" as @"+botUsername)<<'\n';
    if(periodicHandler_)periodicThread_=std::thread(&TelegramBot::periodicLoop,this);
    long long offset=0;
    while(!stopRequested()){
        std::string form="timeout="+urlEncode(std::to_string(pollTimeout_))+"&allowed_updates="+urlEncode("[\"message\",\"callback_query\"]");if(offset>0)form+="&offset="+urlEncode(std::to_string(offset));
        const auto response=postForm("getUpdates",form);if(response.networkError){std::this_thread::sleep_for(std::chrono::seconds(3));continue;}
        try{
            const auto data=json::parse(response.body);if(!data.value("ok",false)){std::cerr<<"getUpdates failed: "<<response.body<<'\n';std::this_thread::sleep_for(std::chrono::seconds(3));continue;}
            for(const auto& update:data.value("result",json::array())){
                if(stopRequested())break;const long long updateId=update.value("update_id",0LL);offset=std::max(offset,updateId+1);
                if(update.contains("callback_query")&&update["callback_query"].is_object()){
                    const auto& callback=update["callback_query"];CallbackQuery incoming;incoming.updateId=updateId;incoming.id=callback.value("id","");incoming.data=callback.value("data","");
                    if(callback.contains("from")){incoming.senderId=callback["from"].value("id",0LL);incoming.username=callback["from"].value("username","");}
                    if(callback.contains("message")&&callback["message"].contains("chat"))incoming.chatId=callback["message"]["chat"].value("id",0LL);
                    answerCallbackQuery(incoming.id);if(callbackHandler_&&!callbackHandler_(incoming))std::cerr<<"Callback handler returned false for update "<<updateId<<'\n';continue;
                }
                if(!update.contains("message")||!update["message"].is_object())continue;const auto& message=update["message"];
                if(!message.contains("chat")||!message["chat"].is_object()||!message.contains("text")||!message["text"].is_string())continue;
                const auto& chat=message["chat"];if(privateChatsOnly_&&chat.value("type","")!="private")continue;
                IncomingMessage incoming;incoming.updateId=updateId;incoming.chatId=chat.value("id",0LL);incoming.text=message["text"].get<std::string>();
                if(message.contains("from")&&message["from"].is_object()){incoming.senderId=std::to_string(message["from"].value("id",0LL));incoming.username=message["from"].value("username","");}
                if(handler_&&!handler_(incoming))std::cerr<<"Message handler returned false for update "<<updateId<<'\n';
            }
        }catch(const std::exception& error){std::cerr<<"getUpdates parse error: "<<error.what()<<'\n';std::this_thread::sleep_for(std::chrono::seconds(2));}
    }
    running_=false;if(periodicThread_.joinable())periodicThread_.join();std::cout<<"Telegram bot stopped.\n";
}
