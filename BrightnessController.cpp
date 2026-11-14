#include "BrightnessController.h"

BrightnessController::BrightnessController(QObject *parent, QString root, QString settingsFile) : QObject(parent) {
    m_store = settingsFile.isEmpty() ? std::make_unique<QSettings>()
                                    : std::make_unique<QSettings>(settingsFile, QSettings::IniFormat);
    m_hadManual = m_store->contains("brightness/manual");
    m_settings = BrightnessPolicy::load(*m_store);
    qRegisterMetaType<BacklightState>();
    auto *io = new BacklightIo(std::move(root));
    io->moveToThread(&m_thread);
    connect(&m_thread, &QThread::started, io, &BacklightIo::start);
    connect(&m_thread, &QThread::finished, io, &QObject::deleteLater);
    connect(this, &BrightnessController::writeRequested, io, &BacklightIo::setPercent);
    connect(io, &BacklightIo::stateRead, this, [this](BacklightState state) {
        const bool wasAvailable = available();
        m_state = std::move(state);
        emit stateChanged();
        if (available() && (!m_initialized || !wasAvailable)) {
            if (!m_initialized && !m_hadManual) {
                m_settings.manual = BrightnessPolicy::safePercent(percentage(), minimum());
                persist();
            }
            m_initialized = true;
            m_period = -1;
            request(BrightnessPolicy::target(m_settings, m_override, QDateTime::currentDateTime()));
            evaluate();
        }
    });
    m_writeTimer.setSingleShot(true); m_writeTimer.setInterval(120);
    connect(&m_writeTimer, &QTimer::timeout, this, [this] {
        if (available()) emit writeRequested(m_target, minimum());
    });
    m_saveTimer.setSingleShot(true); m_saveTimer.setInterval(400);
    connect(&m_saveTimer, &QTimer::timeout, this, &BrightnessController::persist);
    connect(&m_clockTimer, &QTimer::timeout, this, &BrightnessController::evaluate);
    m_clockTimer.start(1000);
    m_thread.start();
}
BrightnessController::~BrightnessController() {
    m_thread.quit(); m_thread.wait();
    persist();
}
void BrightnessController::persist() {
    BrightnessPolicy::save(*m_store, m_settings);
    if (m_store->status() != QSettings::NoError) {
        m_state.error = "Brightness settings could not be saved.";
        emit stateChanged();
    }
}
QString BrightnessController::status() const {
    if (overrideActive()) return "Manual override until " + m_override.until.toString("h:mm AP");
    if (!automatic()) return "Manual brightness";
    return BrightnessPolicy::isDay(QTime::currentTime(), m_settings) ? "Automatic · daytime" : "Automatic · nighttime";
}
void BrightnessController::request(int value) {
    m_target = BrightnessPolicy::safePercent(value, minimum());
    if (!m_writeTimer.isActive()) m_writeTimer.start();
}
void BrightnessController::evaluate() {
    const auto now = QDateTime::currentDateTime();
    bool expired = m_override.until.isValid() && !m_override.active(now);
    if (expired) m_override.clear();
    const int period = BrightnessPolicy::isDay(now.time(), m_settings) ? 1 : 0;
    if (automatic() && !m_override.active(now) && (expired || period != m_period)) {
        request(BrightnessPolicy::target(m_settings, m_override, now));
        emit settingsChanged();
    }
    m_period = period;
}
void BrightnessController::setManual(int value) {
    if (!available()) return;
    m_settings.manual = BrightnessPolicy::safePercent(value, minimum());
    if (automatic()) m_override.begin(QDateTime::currentDateTime(), m_settings);
    request(manual());
    m_saveTimer.start();
    emit settingsChanged();
}
void BrightnessController::reconfigure() {
    m_override.clear(); m_period = -1;
    request(BrightnessPolicy::target(m_settings, m_override, QDateTime::currentDateTime()));
    m_saveTimer.start(); evaluate(); emit settingsChanged();
}
void BrightnessController::setAutomatic(bool value) {
    if (value == automatic()) return;
    m_settings.automatic = value; reconfigure();
}
void BrightnessController::setDay(int value) {
    value = BrightnessPolicy::safePercent(value, minimum());
    if (value == day()) return;
    m_settings.day = value; reconfigure();
}
void BrightnessController::setNight(int value) {
    value = BrightnessPolicy::safePercent(value, minimum());
    if (value == night()) return;
    m_settings.night = value; reconfigure();
}
void BrightnessController::setDayStart(int value) {
    value = std::clamp(value, 0, 1439);
    if (value == nightStart()) { emit settingsChanged(); return; }
    if (value == dayStart()) return;
    m_settings.dayStart = value; reconfigure();
}
void BrightnessController::setNightStart(int value) {
    value = std::clamp(value, 0, 1439);
    if (value == dayStart()) { emit settingsChanged(); return; }
    if (value == nightStart()) return;
    m_settings.nightStart = value; reconfigure();
}
void BrightnessController::setMinimum(int value) {
    value = std::clamp(value, 1, 100);
    if (value == minimum()) return;
    m_settings.minimum = value;
    m_settings.manual = BrightnessPolicy::safePercent(manual(), value);
    m_settings.day = BrightnessPolicy::safePercent(day(), value);
    m_settings.night = BrightnessPolicy::safePercent(night(), value);
    reconfigure();
}
void BrightnessController::resumeSchedule() { reconfigure(); }
