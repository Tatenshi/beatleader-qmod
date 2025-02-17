#pragma once

#include <stdlib.h>
#include <string>
#include <vector>

#include "UnityEngine/Vector3.hpp"
#include "UnityEngine/Quaternion.hpp"
#include "UnityEngine/Color.hpp"

#include "include/Utils/StringUtils.hpp"
#include "include/Utils/ModConfig.hpp"
#include "include/Models/Player.hpp"
#include "include/Models/Clan.hpp"
#include "include/Models/Score.hpp"

using namespace std;
using namespace UnityEngine;

namespace FormatUtils {

        inline string_view GetHeadsetNameById(int id) {
            switch (id) {
                case 1: return "Rift";
                case 2: return "Vive";
                case 4: return "Vive Pro";
                case 8: return "WMR";
                case 16: return "Rift S";
                case 32: return "Quest";
                case 64: return "Index";
                case 128: return "Vive Cosmos";
                case 256: return "Quest 2";
            }

            return "Unknown HMD";
        }

        const int Second = 1;
        const int Minute = 60 * Second;
        const int Hour = 60 * Minute;
        const int Day = 24 * Hour;
        const int Month = 30 * Day;
        const int Year = 365 * Day;

        inline string GetRelativeTimeString(string_view timeSet) {
            int timeSetSeconds = std::stoi(timeSet.data());
            int nowSeconds = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            int delta = nowSeconds - timeSetSeconds;
            if (delta < 1 * Minute) return delta == 1 ? "one second ago" : to_string(delta) + " seconds ago";
            if (delta < 2 * Minute) return "a minute ago";
            if (delta < 45 * Minute) return to_string(delta / Minute) + " minutes ago";
            if (delta < 90 * Minute) return "an hour ago";
            if (delta < 24 * Hour) return to_string(delta / Hour) + " hours ago";
            if (delta < 48 * Hour) return "yesterday";
            if (delta < 30 * Day) return to_string(delta / Day) + " days ago";
            if (delta < 12 * Month) {
                int months = delta / Month;
                return months <= 1 ? "one month ago" : to_string(months) + " months ago";
            }
            int years = delta / Year;
            return years <= 1 ? "one year ago" : to_string(years) + " years ago";
        }

        inline string FormatRank(int rank, bool withPrefix) {
            return (withPrefix ? "<size=70%>#</size>" : "") + to_string(rank);
        }

        inline string FormatUserName(string_view userName) {
            return "<noparse>" + userName + "</noparse>";
        }

        const string ModifiersColor = "";

        inline string FormatModifiers(string_view modifiers) {
            return modifiers.length() == 0 ? "" : "<color=#999999>" + modifiers;
        }

        static Color lowAccColor = UnityEngine::Color(0.93, 1, 0.62, 1);
        static Color highAccColor = UnityEngine::Color(1, 0.39, 0.28, 1);

        inline string rgb2hex(Color const& color) {
            stringstream ss; 
            ss << std::hex << ((int)(color.r * 255.0) << 16 | (int)(color.g * 255.0) << 8 | (int)(color.b * 255.0)); 
            return ss.str();
        }

        inline string getAccColorString(float acc) {
            auto lerpValue = pow(acc, 14.0f);
            auto color = Color::Lerp(lowAccColor, highAccColor, lerpValue);
            return rgb2hex(color);
        }

        inline string formatAcc(float accuracy) {
            return "<color=#" + getAccColorString(accuracy) + ">" + to_string_wprecision(accuracy * 100, 2) + "<size=70%>%</size></color>";
        }

        inline string FormatPP(float value) {
            return "<color=#B856FF>" + to_string_wprecision(value, 2) + "<size=70%>pp</size></color>";
        }

        inline string FormatClanTag(string_view tag) {
            return "<alpha=#00>.<alpha=#FF><b><noparse>" + tag + "</noparse></b><alpha=#00>.<alpha=#FF>";
        }

        inline string FormatNameWithClans(Player const& player, int limit) {
            string clansLabel = "<size=90%>";
            int clanCount = player.clans.size();
            if (clanCount == 2) {
                clansLabel = "<size=80%>";
            } else if (clanCount == 3) {
                clansLabel = "<size=70%>";
            }
            for (size_t i = 0; i < clanCount; i++) {
                Clan clan = player.clans[i];
                clansLabel += "  <color=" + clan.color + ">" + clan.tag + "</color>";
            }
            clansLabel += "</size>";

            return truncate(player.name, limit - clanCount * 3) + clansLabel;
        }

        inline string FormatPlayerScore(Score const& score) {
            string fcLabel = "<color=#FFFFFF>" + (string)(score.fullCombo ? "FC" : "") + (score.modifiers.length() > 0 && score.fullCombo ? "," : "") + score.modifiers;
            string name = getModConfig().ClansActive.GetValue() ? FormatNameWithClans(score.player, 24) : truncate(score.player.name, 24);
            string time = getModConfig().TimesetActive.GetValue() ? " <size=60%>" + GetRelativeTimeString(score.timeset) + "</size>" : "";
            return name + "<pos=40%>" + FormatPP(score.pp) + "   " + formatAcc(score.accuracy) + " " + fcLabel + time; 
        }
    }