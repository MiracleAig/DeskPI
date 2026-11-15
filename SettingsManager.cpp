#include "SettingsManager.h"
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QFileInfo>

namespace {
QJsonObject merged(QJsonObject base, const QJsonObject &patch) {
    for (auto i = patch.begin(); i != patch.end(); ++i) base[i.key()] = i.value();
    return base;
}
}
SettingsManager::SettingsManager(BrightnessController *brightness, QString settingsFile, QObject *parent)
    : QObject(parent), m_brightness(brightness) {
    QSettings s = settingsFile.isEmpty() ? QSettings() : QSettings(settingsFile, QSettings::IniFormat);
    m_file = s.fileName();
    m_clock = {{"format24", s.value("clock/format24", false).toBool()},
               {"seconds", s.value("clock/seconds", false).toBool()}, {"date", s.value("clock/date", false).toBool()},
               {"dateFormat", s.value("clock/dateFormat", "ddd, MMM d yyyy").toString()}};
    m_appearance = {{"theme", s.value("appearance/theme", "dark").toString()}, {"accent", s.value("appearance/accent", "#59caff").toString()}};
    if (!validate("clock", m_clock, snapshot()).isEmpty()) m_clock = {{"format24", false}, {"seconds", false}, {"date", false}, {"dateFormat", "ddd, MMM d yyyy"}};
    if (!validate("appearance", m_appearance, snapshot()).isEmpty()) m_appearance = {{"theme", "dark"}, {"accent", "#59caff"}};
    m_media = {{"enabled", true}, {"autoSwitch", true}, {"returnSeconds", 30},
        {"preferredHost", ""}, {"preferredPlayer", ""}, {"gradients", true}, {"animations", true},
        {"showAlbum", true}, {"showArtwork", true}, {"nonMusic", true}, {"artworkLimitKiB",160}};
    const auto defaults = m_media;
    for (auto i = defaults.begin(); i != defaults.end(); ++i) {
        // INI values are read as strings in a fresh process. Restore the declared
        // type before applying the same validation used by HTTP settings patches.
        const auto raw = s.value("media/" + i.key(), i.value().toVariant());
        QJsonValue value;
        if (i.value().isBool()) {
            const auto text = raw.toString().toLower();
            if (text != "true" && text != "false") continue;
            value = text == "true";
        } else if (i.value().isDouble()) {
            bool ok = false;
            const auto number = raw.toDouble(&ok);
            if (!ok) continue;
            value = number;
        } else {
            value = raw.toString();
        }
        QJsonObject patch{{i.key(), value}};
        if (validate("media", patch, snapshot()).isEmpty()) m_media[i.key()] = value;
    }
    connect(brightness, &BrightnessController::settingsChanged, this, &SettingsManager::changed);
    connect(brightness, &BrightnessController::stateChanged, this, &SettingsManager::changed);
}
QJsonObject SettingsManager::snapshot() const {
    return {{"display", QJsonObject{{"manual", m_brightness->manual()}, {"automatic", m_brightness->automatic()},
        {"day", m_brightness->day()}, {"night", m_brightness->night()}, {"dayStart", m_brightness->dayStart()},
        {"nightStart", m_brightness->nightStart()}, {"minimum", m_brightness->minimum()},
        {"current", m_brightness->percentage()}, {"available", m_brightness->available()},
        {"error", m_brightness->error()}, {"overrideActive", m_brightness->overrideActive()}, {"status", m_brightness->status()}}},
        {"clock", m_clock}, {"appearance", m_appearance}, {"media",m_media}};
}
QJsonObject SettingsManager::exportConfig() const {
    auto s = snapshot(); auto display = s.value("display").toObject();
    for (const auto &key : {"current", "available", "error", "overrideActive", "status"}) display.remove(key);
    s["display"] = display;
    return {{"schemaVersion", 1}, {"settings", s}};
}
QString SettingsManager::validate(const QString &section, const QJsonObject &patch, const QJsonObject &current) {
    if (section != "display" && section != "clock" && section != "appearance" && section != "media") return "Unknown settings section.";
    const auto values = merged(current.value(section).toObject(), patch);
    for (auto i = patch.begin(); i != patch.end(); ++i) {
        const auto &key = i.key(); const auto &v = i.value();
        if (section == "media") {
            if(QStringList{"enabled","autoSwitch","gradients","animations","showAlbum","showArtwork","nonMusic"}.contains(key)) {if(!v.isBool()) return key+" must be boolean.";}
            else if(key=="returnSeconds"||key=="artworkLimitKiB") {if(!v.isDouble()||v.toDouble()!=v.toInt()||v.toInt()<(key=="returnSeconds"?1:16)||v.toInt()>(key=="returnSeconds"?600:160))return "Invalid media limit.";}
            else if(key=="preferredHost"||key=="preferredPlayer") {if(!v.isString()||v.toString().size()>256)return "Invalid media preference.";}
            else return "Unsupported media field: "+key;
        } else if (section == "display") {
            if (key == "automatic") { if (!v.isBool()) return "automatic must be boolean."; }
            else if (key == "manual" || key == "day" || key == "night" || key == "minimum" || key == "dayStart" || key == "nightStart") {
                if (!v.isDouble() || v.toDouble() != v.toInt(-9999)) return key + " must be an integer.";
                const int low = key.endsWith("Start") ? 0 : (key == "minimum" ? 1 : values.value("minimum").toInt(5));
                const int high = key.endsWith("Start") ? 1439 : 100;
                if (v.toInt() < low || v.toInt() > high) return key + " is outside its supported range.";
            } else return "Unsupported display field: " + key;
        } else if (section == "clock") {
            if (key == "format24" || key == "seconds" || key == "date") { if (!v.isBool()) return key + " must be boolean."; }
            else if (key == "dateFormat") {
                if (!v.isString() || !QStringList{"ddd, MMM d yyyy", "yyyy-MM-dd", "dd/MM/yyyy", "MM/dd/yyyy"}.contains(v.toString())) return "Unsupported date format.";
            } else return "Unsupported clock field: " + key;
        } else {
            if (key == "theme") { if (!v.isString() || (v != "dark" && v != "light")) return "theme must be dark or light."; }
            else if (key == "accent") { if (!v.isString() || !QRegularExpression("^#[0-9a-fA-F]{6}$").match(v.toString()).hasMatch()) return "accent must be a six-digit hex color."; }
            else return "Unsupported appearance field: " + key;
        }
    }
    if (section == "display") {
        if (values.value("dayStart") == values.value("nightStart")) return "Day and night start times must differ.";
        for (const auto &key : {"manual", "day", "night"}) if (values.value(key).toInt() < values.value("minimum").toInt()) return "Brightness must respect the minimum.";
    }
    return {};
}
void SettingsManager::persistExtras() {
    QVariantMap values;
    for (auto i = m_clock.begin(); i != m_clock.end(); ++i) values["clock/" + i.key()] = i.value().toVariant();
    for (auto i = m_appearance.begin(); i != m_appearance.end(); ++i) values["appearance/" + i.key()] = i.value().toVariant();
    for(auto i=m_media.begin();i!=m_media.end();++i) values["media/"+i.key()]=i.value().toVariant();
    m_brightness->persistFields(values);
}
QString SettingsManager::apply(const QString &section, const QJsonObject &patch) {
    const auto error = validate(section, patch, snapshot());
    if (!error.isEmpty()) return error;
    if (section == "display") {
        if (!m_brightness->available() && !patch.isEmpty()) return "Hardware brightness is unavailable.";
        m_brightness->applyPatch(patch);
    } else {
        if (section == "clock") m_clock = merged(m_clock, patch);
        else if(section=="media") m_media=merged(m_media,patch);
        else m_appearance = merged(m_appearance, patch);
        persistExtras(); emit changed();
    }
    return {};
}
QString SettingsManager::importConfig(const QJsonObject &config) {
    if (config.size() != 2 || config.value("schemaVersion") != QJsonValue(1) || !config.value("settings").isObject()) return "Unsupported configuration format.";
    const auto s = config.value("settings").toObject();
    if ((s.size() != 3 && s.size() != 4) || (s.size()==4&&!s.contains("media"))) return "Configuration must contain exactly display, clock and appearance.";
    const auto exported = exportConfig().value("settings").toObject();
    for (const QString section : {"display", "clock", "appearance"}) {
        if (!s.value(section).isObject() || s.value(section).toObject().size() != exported.value(section).toObject().size()) return "Incomplete settings section.";
        const auto error = validate(section, s.value(section).toObject(), snapshot());
        if (!error.isEmpty()) return error;
    }
    if(s.contains("media")) {const auto e=validate("media",s.value("media").toObject(),snapshot());if(!s.value("media").isObject()||!e.isEmpty())return "Invalid media settings.";}
    if (!m_brightness->available()) return "Hardware brightness is unavailable; import was not applied.";
    // Backup I/O is done by the HTTP worker before it invokes this method.
    m_brightness->applyPatch(s.value("display").toObject(), false);
    m_clock = s.value("clock").toObject(); m_appearance = s.value("appearance").toObject();
    if(s.contains("media")) m_media=merged(m_media,s.value("media").toObject());
    persistExtras(); emit changed();
    return {};
}
QString SettingsManager::flush() { return m_brightness->flushPersistence(); }
