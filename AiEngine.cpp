#include "AiEngine.hpp"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>
using json = nlohmann::json;
namespace {
size_t writeCb(void* c,size_t s,size_t n,void* u){static_cast<std::string*>(u)->append(static_cast<char*>(c),s*n);return s*n;}
std::string detail(const std::string& body){try{auto j=json::parse(body);if(j.contains("error")){auto&e=j["error"];if(e.is_object()&&e.contains("message"))return e["message"].get<std::string>();if(e.is_string())return e.get<std::string>();}if(j.contains("message"))return j["message"].get<std::string>();}catch(...){ }return body.substr(0,500);}
}
AiEngine::AiEngine(std::string key,std::string base,std::string model,std::string prompt,int timeout):apiKey_(std::move(key)),baseUrl_(trimTrailingSlash(std::move(base))),model_(std::move(model)),systemPrompt_(std::move(prompt)),timeoutSeconds_(timeout>0?timeout:45){}
bool AiEngine::enabled()const{return !apiKey_.empty()&&!model_.empty()&&!baseUrl_.empty();}
std::string AiEngine::trimTrailingSlash(std::string v){while(!v.empty()&&v.back()=='/')v.pop_back();return v;}
std::string AiEngine::limitUtf8(const std::string&t,std::size_t m){if(t.size()<=m)return t;size_t p=m;while(p>0&&(static_cast<unsigned char>(t[p])&0xC0)==0x80)--p;return t.substr(0,p);}
std::string AiEngine::generateReply(const std::vector<MessageRecord>& history,const std::string& overridePrompt)const{
 if(!enabled())return "";
 std::string effectivePrompt=systemPrompt_;
 if(!overridePrompt.empty()){if(!effectivePrompt.empty())effectivePrompt+="\n\n--- Специализированная инструкция текущей задачи ---\n";effectivePrompt+=overridePrompt;}
 json messages=json::array();messages.push_back({{"role","system"},{"content",effectivePrompt}});
 constexpr size_t MAX_MESSAGES=200,MAX_HISTORY_BYTES=48000;std::vector<std::pair<std::string,std::string>> selected;size_t total=0;
 for(auto it=history.rbegin();it!=history.rend()&&selected.size()<MAX_MESSAGES;++it){std::string text=limitUtf8(it->text,6000);if(text.empty())continue;if(!selected.empty()&&total+text.size()>MAX_HISTORY_BYTES)break;total+=text.size();selected.emplace_back(it->incoming?"user":"assistant",std::move(text));}
 std::reverse(selected.begin(),selected.end());for(const auto& item:selected)messages.push_back({{"role",item.first},{"content",item.second}});
 json req={{"model",model_},{"messages",messages},{"temperature",0.35},{"max_tokens",700}};std::string url=baseUrl_;if(url.size()<17||url.substr(url.size()-17)!="/chat/completions")url+="/chat/completions";
 for(int attempt=1;attempt<=3;++attempt){CURL*curl=curl_easy_init();if(!curl)throw std::runtime_error("Не удалось запустить HTTP-клиент AI");std::string body,response=req.dump();curl_slist*h=nullptr;h=curl_slist_append(h,"Content-Type: application/json");h=curl_slist_append(h,"Accept: application/json");std::string auth="Authorization: Bearer "+apiKey_;h=curl_slist_append(h,auth.c_str());if(baseUrl_.find("openrouter.ai")!=std::string::npos){h=curl_slist_append(h,"HTTP-Referer: https://github.com/WWWS12341/WWWS");h=curl_slist_append(h,"X-Title: WWWS CustoJusto CRM");}
 curl_easy_setopt(curl,CURLOPT_URL,url.c_str());curl_easy_setopt(curl,CURLOPT_POST,1L);curl_easy_setopt(curl,CURLOPT_POSTFIELDS,response.c_str());curl_easy_setopt(curl,CURLOPT_POSTFIELDSIZE,(long)response.size());curl_easy_setopt(curl,CURLOPT_HTTPHEADER,h);curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,writeCb);curl_easy_setopt(curl,CURLOPT_WRITEDATA,&body);curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT,15L);curl_easy_setopt(curl,CURLOPT_TIMEOUT,(long)timeoutSeconds_);curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,1L);curl_easy_setopt(curl,CURLOPT_NOSIGNAL,1L);CURLcode code=curl_easy_perform(curl);long status=0;curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&status);curl_slist_free_all(h);curl_easy_cleanup(curl);
 if(code!=CURLE_OK){if(attempt<3){std::this_thread::sleep_for(std::chrono::seconds(attempt));continue;}throw std::runtime_error(std::string("Сеть AI: ")+curl_easy_strerror(code));}
 if((status==429||status>=500)&&attempt<3){std::this_thread::sleep_for(std::chrono::seconds(attempt*2));continue;}
 if(status<200||status>=300)throw std::runtime_error("AI HTTP "+std::to_string(status)+": "+detail(body));
 try{auto j=json::parse(body);auto content=j.at("choices").at(0).at("message").at("content");std::string out;if(content.is_string())out=content.get<std::string>();else if(content.is_array())for(auto&x:content)if(x.is_object()&&x.value("type","")=="text")out+=x.value("text","");if(out.empty())throw std::runtime_error("AI вернул пустой ответ");return limitUtf8(out,4096);}catch(const std::exception&e){throw std::runtime_error(std::string("Некорректный ответ AI: ")+e.what());}
 }
 throw std::runtime_error("AI недоступен после повторных попыток");
}
