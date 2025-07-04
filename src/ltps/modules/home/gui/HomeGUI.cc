#include "HomeGUI.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/form/CustomForm.h"
#include "ll/api/form/SimpleForm.h"
#include "ltps/Global.h"
#include "ltps/TeleportSystem.h"
#include "ltps/base/Config.h"
#include "ltps/common/BackSimpleForm.h"
#include "ltps/modules/home/HomeStorage.h"
#include "ltps/modules/home/event/HomeEvents.h"
#include "ltps/utils/McUtils.h"

#include <mc/world/level/dimension/VanillaDimensions.h>


namespace ltps::home {

using ll::form::CustomForm;
using ll::form::CustomFormResult;
using ll::form::SimpleForm;

void HomeGUI::sendMainMenu(Player& player, BackCB backCB) {
    auto localeCode = player.getLocaleCode();
    BackSimpleForm{std::move(backCB), BackSimpleForm::ButtonPos::Lower}
        .setTitle("Home Menu"_trl(localeCode))
        .setContent(" · 请选择一个操作"_trl(localeCode))
        .appendButton(
            "新建家"_trl(localeCode),
            "textures/ui/color_plus",
            "path",
            [](Player& self) { sendAddHomeGUI(self); }
        )
        .appendButton(
            "前往家"_trl(localeCode),
            "textures/ui/send_icon",
            "path",
            [](Player& self) { sendGoHomeGUI(self); }
        )
        .appendButton(
            "编辑家"_trl(localeCode),
            "textures/ui/book_edit_default",
            "path",
            [](Player& self) { sendEditHomeGUI(self); }
        )
        .appendButton(
            "删除家"_trl(localeCode),
            "textures/ui/trash_default",
            "path",
            [](Player& self) { sendRemoveHomeGUI(self); }
        )
        .sendTo(player);
}

void HomeGUI::sendAddHomeGUI(Player& player) {
    auto localeCode = player.getLocaleCode();

    CustomForm fm{"Home - Add"_trl(localeCode)};
    fm.appendLabel("输入要创建的家园名称，例如: My Home\n注意：家园名称不能超过 {} 个字符。"_trl(
        localeCode,
        getConfig().modules.home.nameLength
    ));

    fm.appendInput(
        "name",
        "请输入家园名称",
        "string",
        "",
        "不能超过 {} 个字符哦!"_trl(localeCode, getConfig().modules.home.nameLength)
    );

    fm.sendTo(player, [localeCode{std::move(localeCode)}](Player& self, CustomFormResult const& result, auto) {
        if (!result) return;

        auto name = std::get<std::string>(result->at("name"));
        if (name.empty()) {
            mc_utils::sendText<mc_utils::Error>(self, "家园名称不能为空哦!"_trl(localeCode));
            return;
        }

        ll::event::EventBus::getInstance().publish(PlayerRequestAddHomeEvent(self, name));
    });
}


void HomeGUI::sendChooseHomeGUI(Player& player, ChooseHomeCallback chooseCB) {
    auto localeCode = player.getLocaleCode();

    auto fm = BackSimpleForm::make<HomeGUI::sendMainMenu>(BackCB{});
    fm.setTitle("Choose Home"_trl(localeCode)).setContent("请选择一个家"_trl(localeCode));

    auto storage = TeleportSystem::getInstance().getStorageManager().getStorage<HomeStorage>();

    auto homes = storage->getHomes(player.getRealName());
    for (auto& home : homes) {
        auto _name = home.name; // 拷贝名称，避免 move 后显示空字符串
        fm.appendButton(_name, [chooseCB, home = std::move(home)](Player& self) { chooseCB(self, home); });
    }

    fm.sendTo(player);
}
void HomeGUI::sendChooseHomeGUI(Player& player, ChooseNameCallBack chooseCB) {
    sendChooseHomeGUI(player, [cb = std::move(chooseCB)](Player& self, HomeStorage::Home home) {
        cb(self, home.name);
    });
}

void HomeGUI::sendGoHomeGUI(Player& player) {
    sendChooseHomeGUI(player, [](Player& self, std::string name) {
        ll::event::EventBus::getInstance().publish(PlayerRequestGoHomeEvent(self, std::move(name)));
    });
}

void HomeGUI::sendRemoveHomeGUI(Player& player) {
    sendChooseHomeGUI(player, [](Player& self, std::string name) {
        ll::event::EventBus::getInstance().publish(PlayerRequestRemoveHomeEvent(self, std::move(name)));
    });
}


void HomeGUI::sendEditHomeGUI(Player& player) {
    sendChooseHomeGUI(player, [](Player& self, HomeStorage::Home home) { _sendEditHomeGUI(self, std::move(home)); });
}
void HomeGUI::_sendEditHomeGUI(Player& player, HomeStorage::Home home) {
    auto localeCode = player.getLocaleCode();

    auto fm = BackSimpleForm::make<HomeGUI::sendEditHomeGUI>();
    fm.setTitle("Home - Edit"_trl(localeCode))
        .setContent("名称: {}\n坐标: {}.{}.{}\n维度: {}\n创建时间: {}\n更改时间: {}"_trl(
            localeCode,
            home.name,
            home.x,
            home.y,
            home.z,
            VanillaDimensions::toString(home.dimid),
            home.createdTime,
            home.modifiedTime
        ))
        .appendButton(
            "修改名称"_trl(localeCode),
            "textures/ui/book_edit_default",
            "path",
            [name = home.name](Player& self) { _sendEditHomeNameGUI(self, name); }
        )
        .appendButton(
            "更新坐标"_trl(localeCode),
            "textures/ui/icon_import",
            "path",
            [name = home.name](Player& self) {
                ll::event::EventBus::getInstance().publish(PlayerRequestEditHomeEvent(
                    self,
                    name,
                    PlayerRequestEditHomeEvent::Type::Position,
                    self.getPosition()
                ));
            }
        )
        .sendTo(player);
}


void HomeGUI::_sendEditHomeNameGUI(Player& player, std::string const& name) {
    auto localeCode = player.getLocaleCode();
    CustomForm{"Edit Home Name"}
        .appendLabel("修改家园名称，新名称不得超过 {} 个字符"_trl(localeCode, getConfig().modules.home.nameLength))
        .appendInput("newName", "编辑名称"_trl(localeCode), "string", name)
        .sendTo(player, [name](Player& self, CustomFormResult const& res, auto) {
            if (!res) return;

            auto newName = std::get<std::string>(res->at("newName"));
            if (newName.empty()) {
                mc_utils::sendText<mc_utils::Error>(self, "名称不能为空哦!"_trl(self.getLocaleCode()));
                return;
            }

            ll::event::EventBus::getInstance().publish(PlayerRequestEditHomeEvent{
                self,
                name,
                PlayerRequestEditHomeEvent::Type::Name,
                std::nullopt,
                std::move(newName)
            });
        });
}


} // namespace ltps::home