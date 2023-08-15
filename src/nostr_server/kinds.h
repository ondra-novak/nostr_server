/*
 * kinds.h
 *
 *  Created on: 11. 8. 2023
 *      Author: ondra
 */

#ifndef SRC_NOSTR_SERVER_KINDS_H_
#define SRC_NOSTR_SERVER_KINDS_H_

namespace nostr_server {


namespace kind {

using Type = unsigned int;

constexpr Type Metadata = 0;
constexpr Type Short_Text_Note = 1;
constexpr Type Recommend_Relay = 2;
constexpr Type Contacts = 3;
constexpr Type Encrypted_Direct_Messages = 4;
constexpr Type Event_Deletion = 5;
constexpr Type Repost = 6;
constexpr Type Reaction = 7;
constexpr Type Badge_Award = 8;
constexpr Type Resources = 9;
constexpr Type Decoy_Key = 12;
constexpr Type Git_Commit = 17;
constexpr Type Chess = 30;
constexpr Type Professional_resume = 66;
constexpr Type Reputation = 32;
constexpr Type Channel_Creation = 40;
constexpr Type Channel_Metadata = 41;
constexpr Type Channel_Message = 42;
constexpr Type Channel_Hide_Message = 43;
constexpr Type Channel_Mute_User = 44;
constexpr Type Encrypted_Group_Chat = 104;
constexpr Type nosbin_Pastes = 1050;
constexpr Type nosbin_Comments = 1051;
constexpr Type Gift_Wrap_Event = 1059;
constexpr Type File_Header = 1063;
constexpr Type Reporting = 1984;
constexpr Type Geospatial_Types = 4326;
constexpr Type Lightning_Zap_Request = 9734;
constexpr Type Lightning_Zap_Invoice_Receipts = 9735;
constexpr Type Mute_List = 10000;
constexpr Type Pin_List = 10001;
constexpr Type Relay_List_Metadata = 10002;
constexpr Type Coracle_Mute_Lists = 12165;
constexpr Type Wallet_Info = 13194;
constexpr Type Client_Authentication = 22242;
constexpr Type Nostr_Connect = 24133;
constexpr Type Categorized_People_List = 30000;
constexpr Type Categorized_Bookmark_List = 30001;
constexpr Type Profile_Badge = 30008;
constexpr Type Badge_Definition = 30009;
constexpr Type Long_form_Content = 30023;
constexpr Type Arbitrary_custom_app_data = 30078;
constexpr Type Relay_Status = 30303;

constexpr Type Replaceable_Begin = 10000;
constexpr Type Replaceable_End = 20000;
constexpr Type Ephemeral_Begin = 20000;
constexpr Type Ephemeral_End = 30000;
constexpr Type Parameterized_Replaceable_Begin = 30000;
constexpr Type Parameterized_Replaceable_End = 40000;

}

}



#endif /* SRC_NOSTR_SERVER_KINDS_H_ */
