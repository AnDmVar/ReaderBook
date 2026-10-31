#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFont>
#include <QFontDatabase>
#include <QPainter>
#include "EmbeddedFont.h"
#include "LauncherAssets.h"
#include "ScreenGeometry.h"
#include <QIcon>
#include <QPixmap>
#include "EmbeddedTrust.h"
#include <QKeyEvent>
#include <QInputMethod>
#include <QInputMethodQueryEvent>
#include <QDateTime>
#include <QEvent>
#include <QLabel>
#include <QFileInfo>
#include <QGuiApplication>
#include <QScreen>
#include <QPushButton>
#include <QProgressDialog>
#include <QMessageBox>
#include <QPointer>
#include <QStackedWidget>
#include <QTextStream>
#include <QTimer>
#include <resolv.h>
#include "browser/FocusNavigator.h"
#include "browser/MobileViewport.h"
#include "diagnostics/BrowserDiagnostics.h"

extern "C" {
#include <inkview.h>
}
#include <QUrl>
#include <QVBoxLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QWebEngineHistory>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineView>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWidget>

#include <cstdio>
#include <functional>
#include "browser/TextInputDialog.h"
#include "browser/LoadSpinner.h"
#include "browser/NativeAuth.h"
#include "browser/CompatAssets.h"
#include "browser/CorePolyfills.h"
#include "browser/SitePresentation.h"
#include "browser/ReaderControls.h"
#include "reader/NativeReader.h"
#include "reader/OfflineLibrary.h"
#include "browser/BrowserPointer.h"
#include "browser/SystemBrowser.h"
#include <QResizeEvent>
#include "browser/SessionPersistence.h"
#include "update/OnlineUpdate.h"
#include <QSettings>
#include <QScrollArea>
#include <qwebenginecertificateerror.h>


static const char *kMyBooks =
    "https://www.litres.ru/my-books/read-and-listen/";
static const char *kLitres =
    "https://www.litres.ru/";

static void logLine(const QString &line)
{
    QDir().mkpath("/mnt/ext1/litres-client");
    QFile f("/mnt/ext1/litres-client/litres.log");
    if (!f.open(QIODevice::Append | QIODevice::Text))
        return;

    QTextStream stream(&f);
    stream.setCodec("UTF-8");
    stream << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss")
           << " " << line << "\n";
}

// NSS is seeded before QApplication/Chromium can initialize. Only this
// process receives the private browser home; system files are not changed.
static bool prepareBrowserTrust()
{
    const QString home = "/mnt/ext1/litres-client/browser-home";
    const QString db = home + "/.pki/nssdb";
    if (!QDir().mkpath(db)) { logLine("NSS mkdir failed"); return false; }
    auto seed = [&db](const QString &name, const unsigned char *data, int size) {
        const QString path=db+"/"+name;
        if (QFileInfo::exists(path)) return QFileInfo(path).size()>0;
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)) return false;
        file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
        return file.write(reinterpret_cast<const char *>(data),size)==size && file.commit();
    };
    if (!seed("cert9.db",pbCertDb,sizeof(pbCertDb)) || !seed("key4.db",pbKeyDb,sizeof(pbKeyDb))) {
        logLine("NSS seed failed; existing databases preserved"); return false;
    }
    QSaveFile probe(db+"/.write-test");
    if (!probe.open(QIODevice::WriteOnly)) { logLine("NSS database directory is not writable"); return false; }
    probe.cancelWriting();
    qputenv("HOME",home.toUtf8());
    logLine("NSS private home="+home+" root=GlobalSign Root R46; HTTPS verification enabled");
    return true;
}

class DiagnosticWebPage : public QWebEnginePage {
public:
    explicit DiagnosticWebPage(QWebEngineProfile *profile,QObject *parent) : QWebEnginePage(profile,parent) {}
    std::function<bool(const QUrl&)> navigation;
protected:
    bool acceptNavigationRequest(const QUrl &url,NavigationType type,bool mainFrame)override{Q_UNUSED(type);return !mainFrame||!navigation||navigation(url);}
    void javaScriptConsoleMessage(JavaScriptConsoleMessageLevel level,const QString &message,int line,const QString &source) override {
        if(level==ErrorMessageLevel){
            const QString kind=message.contains("SyntaxError")?"SyntaxError":message.contains("TypeError")?"TypeError":message.contains("ReferenceError")?"ReferenceError":"Other";
            logLine(QString("JS error kind=%1 source=%2 line=%3").arg(kind).arg(QUrl(source).toString(QUrl::RemoveQuery|QUrl::RemoveFragment)).arg(line));
        }
        QWebEnginePage::javaScriptConsoleMessage(level,message,line,source);
    }
    bool certificateError(const QWebEngineCertificateError &error) override {
        logLine(QString("TLS rejected host=%1 code=%2").arg(error.url().host()).arg(error.error()));
        return false;
    }
};

class LitresWebView : public QWebEngineView
{
public:
    explicit LitresWebView(QWidget *parent = nullptr)
        : QWebEngineView(parent)
    {
        setFocusPolicy(Qt::StrongFocus);
        const QString dir="/mnt/ext1/litres-client";
        if(!SessionPersistence::prepareProfile(dir))logLine("profile preparation failed");
        profile_=new QWebEngineProfile("LitresPocketBook",this);
        profile_->setPersistentStoragePath(dir+"/webprofile");
        profile_->setCachePath(dir+"/webprofile/cache");
        profile_->setPersistentCookiesPolicy(QWebEngineProfile::ForcePersistentCookies);
        profile_->setHttpCacheType(QWebEngineProfile::DiskHttpCache);
        setPage(new DiagnosticWebPage(profile_,this));
        session_=new SessionPersistence(profile_,dir,this);
        logLine(QString("profile persistent=%1 restored cookies=%2").arg(!profile_->isOffTheRecord()).arg(session_->restoredCookieCount()));
        setAttribute(Qt::WA_InputMethodEnabled,false);

        QWebEngineProfile *profile = page()->profile();
        CompatAssets::install(profile,[](const QString &line){logLine(line);});
        profile->setHttpUserAgent(QString::fromLatin1(MobileViewport::mobileUserAgent()));
        setZoomFactor(2.0);
        logLine(QString("WebEngine zoomFactor=%1 targetCSSWidth~=379").arg(zoomFactor()));

        QWebEngineScript coreScript;
        coreScript.setName("PocketBook core polyfills");
        coreScript.setInjectionPoint(QWebEngineScript::DocumentCreation);
        coreScript.setWorldId(QWebEngineScript::MainWorld);
        coreScript.setRunsOnSubFrames(true);
        coreScript.setSourceCode(CorePolyfills::injection());
        page()->scripts().insert(coreScript);
        QWebEngineScript presentationScript;
        presentationScript.setName("PocketBook cover and consent layout");
        presentationScript.setInjectionPoint(QWebEngineScript::DocumentReady);
        presentationScript.setWorldId(QWebEngineScript::MainWorld);
        presentationScript.setRunsOnSubFrames(false);
        presentationScript.setSourceCode(SitePresentation::injection());
        page()->scripts().insert(presentationScript);

        QWebEngineScript mobileScript;
        mobileScript.setName("PocketBook mobile viewport");
        mobileScript.setInjectionPoint(QWebEngineScript::DocumentReady);
        mobileScript.setWorldId(QWebEngineScript::MainWorld);
        mobileScript.setRunsOnSubFrames(false);
        mobileScript.setSourceCode(MobileViewport::injection());
        page()->scripts().insert(mobileScript);
        QWebEngineScript readerScript;
        readerScript.setName("PocketBook reader controls");
        readerScript.setInjectionPoint(QWebEngineScript::DocumentReady);
        readerScript.setWorldId(QWebEngineScript::MainWorld);
        readerScript.setRunsOnSubFrames(false);
        readerScript.setSourceCode(ReaderControls::injection());
        page()->scripts().insert(readerScript);
        QWebEngineScript navigationScript;
        navigationScript.setName("PocketBook focus and input");
        navigationScript.setInjectionPoint(QWebEngineScript::DocumentReady);
        navigationScript.setWorldId(QWebEngineScript::MainWorld);
        navigationScript.setRunsOnSubFrames(false);
        navigationScript.setSourceCode(FocusNavigator::installScript());
        page()->scripts().insert(navigationScript);


        settings()->setAttribute(
            QWebEngineSettings::JavascriptEnabled, true);
        settings()->setAttribute(
            QWebEngineSettings::LocalStorageEnabled, true);
        settings()->setAttribute(
            QWebEngineSettings::AutoLoadImages, true);
        settings()->setAttribute(
            QWebEngineSettings::ScrollAnimatorEnabled, false);

        connect(this, &QWebEngineView::loadFinished,
                this, [this](bool ok) {
            if(!sessionReady_){
                if(bootstrapping_){bootstrapping_=false;session_->restoreAfterPageReady([this](bool restored){sessionReady_=restored;sessionFailed_=!restored;logLine(restored?"session restore ready":"session restore failed; saved data preserved");auto pending=readyCallbacks_;readyCallbacks_.clear();for(const auto &done:pending)done(restored);});}
                return;
            }
            page()->runJavaScript(MobileViewport::injection());
            page()->runJavaScript(FocusNavigator::installScript());
            QTimer::singleShot(800,this,[this](){
                page()->runJavaScript("window.__pbViewportDiagnostics && window.__pbViewportDiagnostics()",
                    [](const QVariant &v) { logLine("viewport\n" + BrowserDiagnostics::formatJsResult(v)); });
            });
            logLine(QString("loadFinished=%1 url=%2")
                    .arg(ok ? "true" : "false")
                    .arg(url().toString(QUrl::RemoveQuery|QUrl::RemoveFragment)));
            if (!ok)
                return;

            QTimer::singleShot(
                350, this, [this]() {
                    page()->runJavaScript(
                        "window.__pbEnsureFocus && window.__pbEnsureFocus()");
                });
        });
        pointer_=new BrowserPointer(this);
        setCursor(Qt::BlankCursor);
        connect(this,&QWebEngineView::urlChanged,this,[this](const QUrl&){updatePointerMode();});
        connect(this,&QWebEngineView::loadFinished,this,[this](bool){updatePointerMode();});
        page()->setHtml("<html><body></body></html>",QUrl("about:blank"));
    }

    ~LitresWebView() override {QObject::disconnect(this,nullptr,nullptr,nullptr);QObject::disconnect(page(),nullptr,this,nullptr);static_cast<DiagnosticWebPage *>(page())->navigation=nullptr;if(sessionReady_)session_->flush();delete page();delete session_;delete profile_;}
    bool sessionReady()const{return sessionReady_;}
    void whenSessionReady(const std::function<void(bool)> &done){if(sessionReady_||sessionFailed_)done(sessionReady_);else readyCallbacks_.append(done);}
    SessionPersistence *session()const{return session_;}
    std::function<void()> backToMenu;

    void focusPrevious()
    {
        page()->runJavaScript(
            "window.__pbFocus && window.__pbFocus(-1)");
    }

    void focusNext()
    {
        page()->runJavaScript(
            "window.__pbFocus && window.__pbFocus(1)");
    }

    bool isReader()const{return url().path().startsWith("/static/reader/text/");}
    QPoint pointerPosition()const{return pointer_->position();}
    void movePointerTo(QPoint point){pointer_->place(point);BrowserPointer::mouse(this,pointer_->position(),QEvent::MouseMove);}
    void activateCurrent()
    {
        const QUrl original=url();
        if(!isReader()){
            pointer_->fit();const QPoint point=pointer_->position();
            const QString hit="window.__pbPointInput && window.__pbPointInput("+QString::number(point.x()/zoomFactor())+","+QString::number(point.y()/zoomFactor())+")";
            page()->runJavaScript(hit,[this,original,point](const QVariant &result){
                if(url()!=original)return;
                const auto input=result.toMap();
                if(input.value("input").toBool()){editInput(input,original);return;}
                setFocus(Qt::OtherFocusReason);
                BrowserPointer::mouse(this,point,QEvent::MouseMove);
                BrowserPointer::mouse(this,point,QEvent::MouseButtonPress);
                BrowserPointer::mouse(this,point,QEvent::MouseButtonRelease);
                pointer_->raise();
            });return;
        }
        page()->runJavaScript("window.__pbActivate && window.__pbActivate()",[this,original](const QVariant &result){if(url()==original&&result.toMap().value("input").toBool())editInput(result.toMap(),original);});
    }

    void goBackOrMenu()
    {
        if (history()->canGoBack())
            back();
        else if (backToMenu)
            backToMenu();
    }

    void moveFocus(const QString &direction,bool repeat=false)
    {
        if(!isReader()&&direction!="pageUp"&&direction!="pageDown"){
            pointer_->step(direction,repeat);updatePointerMode();BrowserPointer::mouse(this,pointer_->position(),QEvent::MouseMove);return;
        }
        QSettings pref("/mnt/ext1/litres-client/preferences.ini",QSettings::IniFormat);
        const bool horizontal=pref.value("reader/horizontalFocus",false).toBool();
        const bool fast=pref.value("reader/fastPaging",true).toBool();
        page()->runJavaScript("window.__pbReaderControl && window.__pbReaderControl('"+direction+"',"+(horizontal?"true":"false")+","+(fast?"true":"false")+");");
    }

protected:
    void resizeEvent(QResizeEvent *event)override{QWebEngineView::resizeEvent(event);if(pointer_){pointer_->fit();updatePointerMode();}}
private:
    void updatePointerMode(){if(!pointer_)return;pointer_->fit();pointer_->setVisible(!isReader());pointer_->raise();page()->runJavaScript(QString("window.__pbSetPointerMode && window.__pbSetPointerMode(%1)").arg(isReader()?"false":"true"));}
    void editInput(const QVariantMap &input,const QUrl &original){
        TextInputDialog dialog(input.value("value").toString(),input.value("password").toBool(),window());
        if(dialog.exec()!=QDialog::Accepted||url()!=original)return;
        QJsonArray data;data.append(dialog.value());const QString encoded=QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact));
        page()->runJavaScript("window.__pbSetInput && window.__pbSetInput("+encoded+"[0])",[this](const QVariant&){QGuiApplication::inputMethod()->hide();update();repaint();});
    }
    BrowserPointer *pointer_=nullptr;
    QWebEngineProfile *profile_=nullptr;
    SessionPersistence *session_=nullptr;
    bool sessionReady_=false,sessionFailed_=false,bootstrapping_=true;
    QList<std::function<void(bool)>> readyCallbacks_;
};


class Home : public QWidget
{
public:
    std::function<void()> openLitres;
    std::function<void()> openSettings;
    std::function<void()> exitApp;

    Home()
    {
        setAutoFillBackground(true);
        setStyleSheet(
            "QWidget { background: white; color: black; }"
            "QLabel#title { font-size: 36px; font-weight: 600; }"
            "QLabel#action { font-size: 25px; }"
            "QLabel#hint { font-size: 25px; }"
            "QPushButton {"
            "  background: white;"
            "  color: black;"
            "  border: none;"
            "  border-bottom: 1px solid #999;"
            "  text-align: left;"
            "  padding-left: 16px;"
            "  font-size: 34px;"
            "}"
            "QPushButton:focus {"
            "  background: #222;"
            "  color: white;"
            "  border: 3px solid black;"
            "}"
        );

        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        auto *top = new QWidget;
        top->setFixedHeight(105);
        auto *topLayout = new QHBoxLayout(top);
        topLayout->setContentsMargins(14, 0, 14, 0);
        topLayout->setSpacing(0);

        auto *title = new QLabel("Литрес");
        title->setObjectName("title");
        auto *action = new QLabel("Меню");
        action->setObjectName("action");

        topLayout->addWidget(title);
        topLayout->addStretch();
        topLayout->addWidget(action);
        root->addWidget(top);

        auto *sep1 = new QFrame;
        sep1->setFrameShape(QFrame::HLine);
        sep1->setFixedHeight(1);
        root->addWidget(sep1);

        auto *body = new QWidget;
        auto *bodyLayout = new QVBoxLayout(body);
        bodyLayout->setContentsMargins(32, 36, 32, 0);
        bodyLayout->setSpacing(0);

        firstButton_ = addRow(bodyLayout, "Литрес",
            [this]() { if (openLitres) openLitres(); });
        addRow(bodyLayout, "Настройки",
            [this]() { if (openSettings) openSettings(); });
        addRow(bodyLayout, "Выход",
            [this]() { if (exitApp) exitApp(); });

        bodyLayout->addStretch();
        root->addWidget(body, 1);

        auto *sep2 = new QFrame;
        sep2->setFrameShape(QFrame::HLine);
        sep2->setFixedHeight(1);
        root->addWidget(sep2);

        auto *bottom = new QWidget;
        bottom->setFixedHeight(96);
        auto *bottomLayout = new QHBoxLayout(bottom);
        bottomLayout->setContentsMargins(14, 0, 14, 0);
        auto *hint = new QLabel("◀ ▶  Выбор        OK  Открыть");
        hint->setObjectName("hint");
        bottomLayout->addWidget(hint);
        bottomLayout->addStretch();
        root->addWidget(bottom);

        QTimer::singleShot(0, this, [this]() {
            if (firstButton_)
                firstButton_->setFocus(Qt::OtherFocusReason);
        });
    }

private:
    QPushButton *firstButton_ = nullptr;

    QPushButton *addRow(QVBoxLayout *layout,
                        const QString &text,
                        const std::function<void()> &callback)
    {
        auto *button = new QPushButton(text);
        button->setFixedHeight(155);
        QPixmap icon(64,64); icon.fill(Qt::transparent);
        QPainter painter(&icon); painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(Qt::black, 4));
        if (text == "Литрес") {
            painter.drawRect(10,10,12,44);
            for (int y=12; y<55; y+=10) painter.drawLine(30,y,54,y);
        } else if (text == "Настройки") {
            painter.drawEllipse(14,14,36,36); painter.drawEllipse(25,25,14,14);
            for (int angle=0; angle<360; angle+=45) {
                painter.save(); painter.translate(32,32); painter.rotate(angle);
                painter.drawLine(0,-18,0,-28); painter.restore();
            }
        } else {
            painter.drawLine(12,10,12,54); painter.drawLine(12,10,30,10);
            painter.drawLine(12,54,30,54); painter.drawLine(24,32,56,32);
            painter.drawLine(44,20,56,32); painter.drawLine(44,44,56,32);
        }
        painter.end();
        QIcon rowIcon; rowIcon.addPixmap(icon, QIcon::Normal);
        QPixmap selected=icon;
        QPainter inverse(&selected); inverse.setCompositionMode(QPainter::CompositionMode_SourceIn);
        inverse.fillRect(selected.rect(), Qt::white); inverse.end();
        rowIcon.addPixmap(selected, QIcon::Active); rowIcon.addPixmap(selected, QIcon::Selected);
        button->setIcon(rowIcon); button->setIconSize(QSize(64,64));
        button->setFocusPolicy(Qt::StrongFocus);
        QObject::connect(button, &QPushButton::clicked, callback);
        layout->addWidget(button);
        return button;
    }
};

class LoadingScreen : public QWidget
{
public:
    LoadingScreen()
    {
        setAutoFillBackground(true);
        setStyleSheet("QWidget { background:white; color:black; }");

        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(20, 20, 20, 20);
        layout->addStretch();

        auto *logo = new QLabel("Литрес");
        QFont logoFont = logo->font();
        logoFont.setPixelSize(64);
        logoFont.setBold(true);
        logo->setFont(logoFont);
        logo->setAlignment(Qt::AlignCenter);

        auto *message = new QLabel("Открываем сайт…");
        QFont msgFont = message->font();
        msgFont.setPixelSize(30);
        message->setFont(msgFont);
        message->setAlignment(Qt::AlignCenter);

        auto *dots = new QLabel("•  •  •");
        QFont dotsFont = dots->font();
        dotsFont.setPixelSize(36);
        dots->setFont(dotsFont);
        dots->setAlignment(Qt::AlignCenter);

        layout->addWidget(logo);
        layout->addSpacing(16);
        layout->addWidget(message);
        layout->addSpacing(12);
        layout->addWidget(dots);
        layout->addStretch();
    }
};

class BrowserShell : public QWidget
{
public:
    explicit BrowserShell(LitresWebView *web)
        : web_(web)
    {
        setAutoFillBackground(true);
        setStyleSheet(
            "QWidget { background:white; color:black; }"
            "QLabel#browserTitle { font-size:34px; font-weight:600; }"
            "QLabel#browserAction { font-size:25px; }"
            "QLabel#browserHint { font-size:24px; }"
        );

        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(0,0,0,0);
        root->setSpacing(0);

        auto *top = new QWidget;
        top->setFixedHeight(105);
        auto *tl = new QHBoxLayout(top);
        tl->setContentsMargins(12,0,12,0);
        title_ = new QLabel("Литрес");
        title_->setObjectName("browserTitle");
        auto *action = new QLabel("Меню");
        action->setObjectName("browserAction");
        tl->addWidget(title_);
        tl->addStretch();
        spinner_=new LoadSpinner;
        tl->addWidget(spinner_);
        tl->addSpacing(14);
        tl->addWidget(action);
        root->addWidget(top);

        auto *sep1 = new QFrame;
        sep1->setFrameShape(QFrame::HLine);
        sep1->setFixedHeight(1);
        root->addWidget(sep1);

        root->addWidget(web_, 1);

        auto *sep2 = new QFrame;
        sep2->setFrameShape(QFrame::HLine);
        sep2->setFixedHeight(1);
        root->addWidget(sep2);

        auto *bottom = new QWidget;
        bottom->setFixedHeight(96);
        auto *bl = new QHBoxLayout(bottom);
        bl->setContentsMargins(12,0,12,0);
        auto *hint = new QLabel("Стрелки Курсор · OK Клик · Удерж. OK Меню");
        connect(web_,&QWebEngineView::urlChanged,this,[this,hint](const QUrl&){hint->setText(web_->isReader()?"↑/↓ Строка · Листать экран · Удерж. OK Меню":"Стрелки Курсор · OK Клик · Удерж. OK Меню");});
        hint->setObjectName("browserHint");
        bl->addWidget(hint);
        bl->addStretch();
        root->addWidget(bottom);
    }

    void setLoading(bool loading) { spinner_->setBusy(loading); }

    void setTitleText(const QString &text)
    {
        title_->setText(text);
    }

private:
    LoadSpinner *spinner_;
    LitresWebView *web_;
    QLabel *title_;
};

class SettingsScreen : public QWidget
{
public:
    SettingsScreen()
    {
        setAutoFillBackground(true);
        setStyleSheet(
            "QWidget { background:white; color:black; }"
            "QLabel#settingsTitle { font-size:36px; font-weight:600; }"
            "QLabel#settingsAction { font-size:25px; }"
            "QPushButton {"
            "  background:white;"
            "  color:black;"
            "  border:none;"
            "  border-bottom:1px solid #999;"
            "  text-align:left;"
            "  padding-left:10px;"
            "  font-size:28px;"
            "}"
            "QPushButton:focus { background:#222; color:white; }"
        );

        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(0,0,0,0);
        root->setSpacing(0);

        auto *top = new QWidget;
        top->setFixedHeight(105);
        auto *tl = new QHBoxLayout(top);
        tl->setContentsMargins(12,0,12,0);

        auto *title = new QLabel("Настройки");
        title->setObjectName("settingsTitle");
        auto *action = new QLabel("◀ Назад");
        action->setObjectName("settingsAction");

        tl->addWidget(title);
        tl->addStretch();
        tl->addWidget(action);
        root->addWidget(top);

        auto *sep = new QFrame;
        sep->setFrameShape(QFrame::HLine);
        sep->setFixedHeight(1);
        root->addWidget(sep);

        auto *body = new QWidget;
        auto *vl = new QVBoxLayout(body);
        vl->setContentsMargins(12, 8, 12, 0);
        vl->setSpacing(0);

        auto group = [vl](const QString &text) {
            auto *label = new QLabel(text);
            label->setStyleSheet("font-size:29px; font-weight:bold; padding-top:12px;");
            label->setFixedHeight(52); vl->addWidget(label);
        };
        group("Основные");
        addRow(vl, "Сохранение сессии", "Вкл");
        addRow(vl, "Открывать «Мои книги»", "Вкл");
        group("Браузер");
        addRow(vl, "Мобильный режим", "Вкл");
        addRow(vl, "Масштаб страницы", "200%");
        auto *systemBrowser=new QPushButton("Литрес в штатном браузере");systemBrowser->setObjectName("systemBrowser");systemBrowser->setFixedHeight(80);vl->addWidget(systemBrowser);
        connect(systemBrowser,&QPushButton::clicked,[this](){if(openSystemBrowser)openSystemBrowser();});
        auto *systemHint=new QLabel("Для сравнения: масштаб и вход задаются в штатном браузере.");systemHint->setWordWrap(true);systemHint->setStyleSheet("font-size:22px;padding:8px;");systemHint->setMinimumHeight(70);vl->addWidget(systemHint);
        group("О приложении");
        addRow(vl, "Версия", "0.6.16");
        auto *controls=new QPushButton;
        auto updateLabel=[controls](){QSettings p("/mnt/ext1/litres-client/preferences.ini",QSettings::IniFormat);controls->setText(p.value("reader/horizontalFocus",false).toBool()?"В книге ←/→: выбор кнопок":"В книге ←/→: без действия");};
        controls->setObjectName("readerHorizontal");controls->setFixedHeight(80);vl->addWidget(controls);updateLabel();
        connect(controls,&QPushButton::clicked,[updateLabel](){QSettings p("/mnt/ext1/litres-client/preferences.ini",QSettings::IniFormat);p.setValue("reader/horizontalFocus",!p.value("reader/horizontalFocus",false).toBool());p.sync();updateLabel();});
        auto *paging=new QPushButton;paging->setObjectName("readerPaging");paging->setFixedHeight(80);vl->addWidget(paging);
        auto pagingLabel=[paging](){QSettings p("/mnt/ext1/litres-client/preferences.ini",QSettings::IniFormat);paging->setText(p.value("reader/fastPaging",true).toBool()?"Веб-листание: быстро":"Веб-листание: перекрытие");};pagingLabel();
        connect(paging,&QPushButton::clicked,[pagingLabel](){QSettings p("/mnt/ext1/litres-client/preferences.ini",QSettings::IniFormat);p.setValue("reader/fastPaging",!p.value("reader/fastPaging",true).toBool());p.sync();pagingLabel();});
        auto *updates=new QPushButton("Проверить обновления");updates->setObjectName("checkUpdates");updates->setFixedHeight(80);vl->addWidget(updates);
        connect(updates,&QPushButton::clicked,[this](){if(openUpdates)openUpdates();});
        auto *rollback=new QPushButton("Вернуть предыдущую версию");rollback->setFixedHeight(80);vl->addWidget(rollback);
        connect(rollback,&QPushButton::clicked,[this](){if(QMessageBox::question(this,"Откат приложения","Вернуть сохранённую предыдущую версию приложения?")!=QMessageBox::Yes)return;const QString target=QCoreApplication::applicationFilePath();if(QFileInfo(target).absolutePath()!="/mnt/ext1/applications"||!AppUpdate::rollback(target)){QMessageBox::information(this,"Откат","Резервная копия отсутствует или не прошла проверку.");return;}QMessageBox::information(this,"Откат","Предыдущая версия восстановлена. Приложение закроется; откройте его снова.");QCoreApplication::quit();});
        auto *diagnostics = new QPushButton("Диагностика");
        diagnostics->setFixedHeight(80); vl->addWidget(diagnostics);
        connect(diagnostics, &QPushButton::clicked, [this]() { if (openDiagnostics) openDiagnostics(); });

        auto *login = new QPushButton("Войти / сменить аккаунт");login->setObjectName("login");
        login->setFixedHeight(80); vl->addWidget(login);
        connect(login, &QPushButton::clicked, [this](){if(openLogin)openLogin();});
        auto *logout=new QPushButton("Выйти из аккаунта");logout->setObjectName("logout");logout->setFixedHeight(80);vl->addWidget(logout);
        connect(logout,&QPushButton::clicked,[this](){if(openLogout)openLogout();});
        vl->addStretch();
        auto *scroll = new QScrollArea;scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);scroll->setWidget(body);root->addWidget(scroll,1);
        connect(qApp,&QApplication::focusChanged,this,[scroll](QWidget*,QWidget *now){if(now&&scroll->isAncestorOf(now))scroll->ensureWidgetVisible(now,0,12);});
        auto *footer = new QLabel("↑/↓ Выбор · OK Открыть · Левая клавиша Назад");
        footer->setFixedHeight(96);
        footer->setStyleSheet("font-size:25px; border-top:1px solid black; padding-left:24px;");
        root->addWidget(footer);
    }

private:
    void addRow(QVBoxLayout *layout, const QString &text, const QString &value)
    {
        auto *row = new QFrame;
        row->setFixedHeight(80);
        row->setStyleSheet("QFrame { border-bottom:1px solid #999; } QLabel { border:none; font-size:27px; }");
        auto *columns = new QHBoxLayout(row);
        columns->setContentsMargins(12,0,12,0);
        columns->addWidget(new QLabel(text), 1);
        columns->addWidget(new QLabel(value));
        layout->addWidget(row);
    }
public:
    std::function<void()> openDiagnostics;
    std::function<void()> openLogin;
    std::function<void()> openUpdates;
    std::function<void()> openLogout;
    std::function<void()> openSystemBrowser;

};


static bool ensurePocketBookNetwork()
{
    const int before = QueryNetwork();
    logLine(QString("network before flags=0x%1")
            .arg(before, 0, 16));

    if ((before & NET_CONNECTED) == 0) {
        logLine("network not connected -> NetConnect(NULL)");
        const int rc = NetConnect(nullptr);
        logLine(QString("NetConnect rc=%1").arg(rc));
    } else {
        logLine("network already connected");
    }

    // PocketBook/old glibc may cache resolver state before Wi-Fi comes up.
    const int resolverRc = res_init();
    logLine(QString("res_init rc=%1").arg(resolverRc));

    const int after = QueryNetwork();
    logLine(QString("network after flags=0x%1 connected=%2")
            .arg(after, 0, 16)
            .arg((after & NET_CONNECTED) ? "yes" : "no"));

    return (after & NET_CONNECTED) != 0;
}

class MainWindow : public QStackedWidget
{
public:
    MainWindow()
    {
        setAutoFillBackground(true);
        layout()->setSizeConstraint(QLayout::SetNoConstraint);setMinimumSize(0,0);
        setStyleSheet("QStackedWidget { background: white; color: black; }");

        home_ = new Home;
        diagnostics_ = createDiagnostics();
        loading_ = new LoadingScreen;
        settings_ = new SettingsScreen;
        settings_->openDiagnostics = [this]() { refreshDiagnostics(); setCurrentWidget(diagnostics_); };
        settings_->openLogin = [this]() {beginNativeLogin();};
        settings_->openLogout = [this](){logout();};
        settings_->openSystemBrowser=[this](){
            if(!ensurePocketBookNetwork()){QMessageBox::information(this,"Штатный браузер","Подключитесь к Wi-Fi и повторите попытку.");return;}
            const auto result=SystemBrowser::launch(QUrl(kMyBooks),{QString::fromLatin1(BROWSER_FOR_AUTH),"/ebrmain/bin/browser.app","/usr/bin/browser.app"},[](const char *app,const char *url,int flags){return OpenBook(app,url,flags);});
            logLine(QString("system browser request=%1 path=%2 rc=%3; profile and firmware unchanged").arg(result.requested).arg(result.path).arg(result.code));
            if(!result.requested)QMessageBox::information(this,"Штатный браузер","Приложение штатного браузера на этой прошивке не найдено.");
        };
        settings_->openUpdates = [this](){if(!ensurePocketBookNetwork()){QMessageBox::information(this,"Обновления","Подключитесь к Wi-Fi и повторите попытку.");return;}OnlineUpdate updater(this);updater.exec();};

        addWidget(home_);
        addWidget(diagnostics_);
        addWidget(loading_);
        addWidget(settings_);

        home_->openLitres =
            [this]() { openLitres(); };
        home_->openSettings =
            [this]() {
                setCurrentWidget(settings_);
                const auto buttons = settings_->findChildren<QPushButton *>();
                if (!buttons.isEmpty())
                    buttons.first()->setFocus(Qt::OtherFocusReason);
            };
        home_->exitApp =
            []() { QApplication::quit(); };

        setCurrentWidget(home_);
        qApp->installEventFilter(this);
        holdOk_.setSingleShot(true);
        connect(&holdOk_, &QTimer::timeout, this, [this](){
            if (pendingOk_ && currentWidget()==browserShell_) {
                pendingOk_=false; showBrowserMenu();
            }
        });
        logLine("home created without WebEngine");
        // Login is only requested after the user explicitly opens LitRes or Login.

    }

    ~MainWindow()override {if(readingOrientation_>=0)SetOrientation(readingOrientation_);if(historyTransport_){delete historyTransport_;historyTransport_=nullptr;}if(reader_){delete reader_;reader_=nullptr;}if(nativeAuth_){delete nativeAuth_;nativeAuth_=nullptr;}}
protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        Q_UNUSED(watched);
        if (web_) {
            auto *widget=qobject_cast<QWidget *>(watched);
            if (widget && (widget==web_ || web_->isAncestorOf(widget))) {
                if(event->type()==QEvent::InputMethodQuery) {
                    static_cast<QInputMethodQueryEvent *>(event)->setValue(Qt::ImEnabled,false);
                    return true;
                }
                if(event->type()==QEvent::FocusIn || event->type()==QEvent::Show) {
                    widget->setAttribute(Qt::WA_InputMethodEnabled,false);
                    QGuiApplication::inputMethod()->hide();
                }
            }
        }
        if (QApplication::activeModalWidget() || QApplication::activePopupWidget()) return false;

        if (event->type()==QEvent::KeyRelease && currentWidget()==browserShell_) {
            auto *released=static_cast<QKeyEvent *>(event);
            if (released->key()==Qt::Key_Return || released->key()==Qt::Key_Enter ||
                released->key()==Qt::Key_Space || hasPocketCode(released,0x0a)) {
                if (released->isAutoRepeat()) return true;
                holdOk_.stop();
                if (pendingOk_) { pendingOk_=false; web_->activateCurrent(); }
                return true;
            }
        }
        if (event->type()==QEvent::KeyRelease && reader_ && currentWidget()==reader_) {
            auto *released=static_cast<QKeyEvent *>(event);
            if(!released->isAutoRepeat()&&(released->key()==Qt::Key_Return||released->key()==Qt::Key_Enter||released->key()==Qt::Key_Space||hasPocketCode(released,0x0a))&&pendingOk_){pendingOk_=false;reader_->menu();}
            return true;
        }
        if (event->type() != QEvent::KeyPress)
            return QStackedWidget::eventFilter(watched, event);

        auto *key = static_cast<QKeyEvent *>(event);

        logLine(QString("key qt=%1 nativeVirtual=%2 scan=%3 current=%4")
                .arg(key->key())
                .arg(key->nativeVirtualKey())
                .arg(key->nativeScanCode())
                .arg(currentWidget() == home_ ? "home" :
                     currentWidget() == diagnostics_ ? "diagnostics" :
                     currentWidget() == settings_ ? "settings" :
                     currentWidget() == browserShell_ ? "web" : "other"));

        const bool up =
            key->key() == Qt::Key_Up ||
            key->key() == Qt::Key_PageUp ||
            hasPocketCode(key, 0x11) ||
            hasPocketCode(key, 0x18) ||
            hasPocketCode(key, 0x1c);

        const bool down =
            key->key() == Qt::Key_Down ||
            key->key() == Qt::Key_PageDown ||
            hasPocketCode(key, 0x12) ||
            hasPocketCode(key, 0x19) ||
            hasPocketCode(key, 0x1d);

        const bool left =
            key->key() == Qt::Key_Left ||
            hasPocketCode(key, 0x13);
        const bool back=key->key()==Qt::Key_Back || key->key()==Qt::Key_Escape || hasPocketCode(key,0x1b);
        const bool leftPage=key->key()==Qt::Key_PageUp||hasPocketCode(key,0x18)||hasPocketCode(key,0x1c);

        const bool right =
            key->key() == Qt::Key_Right ||
            hasPocketCode(key, 0x14);

        const bool ok =
            key->key() == Qt::Key_Return ||
            key->key() == Qt::Key_Enter ||
            key->key() == Qt::Key_Space ||
            hasPocketCode(key, 0x0a);

        if (reader_ && currentWidget()==reader_) {
            if(ok){if(!key->isAutoRepeat())pendingOk_=true;return true;}
            pendingOk_=false;
            if(key->key()==Qt::Key_Menu){if(!key->isAutoRepeat())reader_->menu();return true;}
            if(back){reader_->pause();QTimer::singleShot(0,this,[this](){leaveReaderToLibrary();});return true;}
            if(leftPage)reader_->control("pageUp");
            else if(key->key()==Qt::Key_PageDown||hasPocketCode(key,0x19)||hasPocketCode(key,0x1d))reader_->control("pageDown");
            else if(up)reader_->control("up");else if(down)reader_->control("down");
            return true;
        }

        if (currentWidget() == home_) {
            if (up || down || left || right) {
                moveHomeFocus((down || right) ? 1 : -1);
                key->accept();
                return true;
            }

            if (ok) {
                QWidget *focused = QApplication::focusWidget();
                if (auto *button = qobject_cast<QPushButton *>(focused)) {
                    logLine(QString("home click %1").arg(button->text()));
                    button->click();
                    key->accept();
                    return true;
                }

                moveHomeFocus(1);
                key->accept();
                return true;
            }

            return false;
        }

        if (currentWidget() == diagnostics_) {
            if (leftPage || back) {
                setCurrentWidget(home_);
                focusFirstHomeButton();
                logLine("diagnostics -> home");
                key->accept();
                return true;
            }
            return false;
        }

        if (currentWidget() == settings_) {
            if(ok){
                if(!key->isAutoRepeat()){
                    auto *button=qobject_cast<QPushButton *>(watched);
                    if(!button||!settings_->isAncestorOf(button))button=qobject_cast<QPushButton *>(QApplication::focusWidget());
                    if(!button||!settings_->isAncestorOf(button))button=qobject_cast<QPushButton *>(settings_->focusWidget());
                    if(button&&settings_->isAncestorOf(button)&&button->isEnabled())button->click();
                    else {const auto buttons=settings_->findChildren<QPushButton *>();if(!buttons.isEmpty())buttons.first()->setFocus(Qt::OtherFocusReason);}
                }
                key->accept();return true;
            }
            if (leftPage || back) {
                setCurrentWidget(home_);
                focusFirstHomeButton();
                logLine("settings -> home");
                key->accept();
                return true;
            }
            if (up || down) {
                const auto buttons = settings_->findChildren<QPushButton *>();
                if (!buttons.isEmpty()) {
                    QWidget *focused = QApplication::focusWidget();
                    int index = 0;
                    for (int i = 0; i < buttons.size(); ++i) {
                        if (buttons.at(i) == focused) {
                            index = i;
                            break;
                        }
                    }
                    index = (index + (down ? 1 : -1) + buttons.size()) % buttons.size();
                    buttons.at(index)->setFocus(Qt::OtherFocusReason);
                }
                key->accept();
                return true;
            }
            return false;
        }

        if (web_ && currentWidget() == browserShell_) {
            if (key->key()==Qt::Key_Menu) {
                pendingOk_=false; holdOk_.stop(); showBrowserMenu(); return true;
            }
            if (!ok) { pendingOk_=false; holdOk_.stop(); }
            if (key->key()==Qt::Key_PageUp || hasPocketCode(key,0x18) || hasPocketCode(key,0x1c)) {
                web_->moveFocus("pageUp"); return true;
            }
            if (key->key()==Qt::Key_PageDown || hasPocketCode(key,0x19) || hasPocketCode(key,0x1d)) {
                web_->moveFocus("pageDown"); return true;
            }
            if (back) { web_->goBackOrMenu(); return true; }
            if (up) {
                web_->moveFocus("up",key->isAutoRepeat());
                key->accept();
                return true;
            }
            if (down) {
                web_->moveFocus("down",key->isAutoRepeat());
                key->accept();
                return true;
            }
            if (ok) {
                if (!key->isAutoRepeat() && !pendingOk_) {
                    pendingOk_=true; holdOk_.start(750);
                }
                key->accept();
                return true;
            }
            if (left) {
                web_->moveFocus("left",key->isAutoRepeat());
                key->accept();
                return true;
            }
            if (right) {
                web_->moveFocus("right",key->isAutoRepeat());
                key->accept();
                return true;
            }
        }

        return false;
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        if (currentWidget() == diagnostics_ &&
            (event->key() == Qt::Key_PageUp ||
             event->key() == Qt::Key_Back)) {
            setCurrentWidget(home_);
            event->accept();
            return;
        }

        QStackedWidget::keyPressEvent(event);
    }

private:
    QTimer holdOk_;
    bool pendingOk_=false;
    void showBrowserMenu()
    {
        BrowserMenuDialog menu(this);
        switch (menu.exec()) {
        case 1: web_->reload(); break;
        case 2: openUrl(kMyBooks); break;
        case 3: openUrl(kLitres); break;
        case 4: refreshDiagnostics(); setCurrentWidget(diagnostics_); break;
        case 5: setCurrentWidget(home_); focusFirstHomeButton(); break;
        case 6: beginNativeLogin(); break;
        default: web_->setFocus(Qt::OtherFocusReason); break;
        }
    }
    NativeReader *reader_=nullptr;
    bool bypassNativeReader_=false;
    int readingOrientation_=-1;
    void fitPocketBookScreen(){int w=ScreenWidth(),h=ScreenHeight();if(w<200||h<200||w>4000||h>4000)return;bool notified=ScreenGeometry::synchronize(QSize(w,h),GetOrientation());resize(w,h);setContentsMargins(0,0,0,qBound(0,PanelHeight(),h/4));QTimer::singleShot(0,this,[this,w,h,notified](){resize(w,h);update();if(reader_)reader_->update();auto *screen=QGuiApplication::primaryScreen();logLine(QString("screen sync hook=%1 sdk=%2x%3 qt=%4x%5 window=%6x%7").arg(notified).arg(w).arg(h).arg(screen?screen->geometry().width():0).arg(screen?screen->geometry().height():0).arg(width()).arg(height()));});}
    void restoreReadingOrientation(){if(readingOrientation_<0)return;SetOrientation(readingOrientation_);readingOrientation_=-1;QTimer::singleShot(100,this,[this](){fitPocketBookScreen();});}
    void leaveReaderToLibrary(){restoreReadingOrientation();openUrl(kMyBooks);}
    void rotateReadingScreen(){int current=GetOrientation();const int clockwise[4]={1,3,0,2};int next=current>=0&&current<4?clockwise[current]:0;SetOrientation(next);QSettings pref("/mnt/ext1/litres-client/preferences.ini",QSettings::IniFormat);pref.setValue("reader/orientation",next);QTimer::singleShot(100,this,[this](){fitPocketBookScreen();});logLine(QString("reader orientation=%1").arg(next));}
    void openNativeReader(const QUrl &url,bool offline=false){
        pendingOk_=false;holdOk_.stop();
        if(reader_){removeWidget(reader_);delete reader_;reader_=nullptr;}
        reader_=new NativeReader(web_->page()->profile(),url,this,offline);addWidget(reader_);
        if(readingOrientation_<0)readingOrientation_=GetOrientation();
        reader_->rotateScreen=[this](){rotateReadingScreen();};
        reader_->openHome=[this](){QTimer::singleShot(0,this,[this](){restoreReadingOrientation();setCurrentWidget(home_);focusFirstHomeButton();});};
        reader_->openSaved=[this](){QTimer::singleShot(0,this,[this](){restoreReadingOrientation();setCurrentWidget(home_);openOfflineBooks();});};
        auto networkSeen=std::make_shared<bool>(bool(QueryNetwork()&NET_CONNECTED));reader_->networkAvailable=[networkSeen](){bool online=bool(QueryNetwork()&NET_CONNECTED);if(online&&!*networkSeen)::res_init();*networkSeen=online;return online;};
        reader_->diagnostic=[](const QString &s){logLine("native reader "+s);};
        reader_->leave=[this,url](bool webVersion){QTimer::singleShot(0,this,[this,url,webVersion](){if(webVersion){restoreReadingOrientation();setCurrentWidget(browserShell_);web_->setFocus();bypassNativeReader_=true;web_->load(url);}else leaveReaderToLibrary();});};
        setCurrentWidget(reader_);reader_->setFocus();browserShell_->setLoading(false);
        QSettings orientation("/mnt/ext1/litres-client/preferences.ini",QSettings::IniFormat);if(orientation.contains("reader/orientation")){SetOrientation(qBound(0,orientation.value("reader/orientation").toInt(),3));QTimer::singleShot(100,this,[this](){fitPocketBookScreen();});}
        logLine("native reader opened; text layout and page keys use Qt, session reused");
    }
    NativeAuth *nativeAuth_=nullptr;
    QPointer<QProgressDialog> authProgress_;
    bool nativeAuthBusy_=false;
    void startAuthProgress(const QString &text) {
        authProgress_=new QProgressDialog(text,"Отмена",0,0,this);
        authProgress_->setWindowTitle("Вход в Литрес");
        authProgress_->setWindowModality(Qt::ApplicationModal);
        authProgress_->setMinimumDuration(0);
        authProgress_->setAutoClose(false);
        connect(authProgress_,&QProgressDialog::canceled,this,[this](){
            if(nativeAuth_)nativeAuth_->cancel();nativeAuthBusy_=false;
            if(authProgress_)authProgress_->deleteLater();authProgress_=nullptr;
        });
        authProgress_->show();
    }
    void finishAuthProgress() {
        if(authProgress_){authProgress_->blockSignals(true);authProgress_->hide();authProgress_->deleteLater();authProgress_=nullptr;}
        nativeAuthBusy_=false;
    }
    void showAuthFailure(const QVariantMap &result) {
        QString message="Не удалось войти. Проверьте логин и пароль и повторите попытку.";
        if(result.value("networkError").toBool())message="Нет ответа от Литрес. Проверьте подключение и повторите вход.";
        if(result.value("challenge").toBool())message="Литрес запросил дополнительное подтверждение. Продолжите вход на сайте через пункт «Литрес».";
        QMessageBox::information(this,"Вход в Литрес",message);
        logLine("native auth failed (credentials and response not logged)");
    }
    void requestNativePassword(const QString &login,bool openAfterLogin=false) {
        TextInputDialog passwordDialog("",true,this,"Вход в Литрес · пароль");
        if(passwordDialog.exec()!=QDialog::Accepted || passwordDialog.value().isEmpty())return;
        QJsonObject body;body["login"]=login;body["password"]=passwordDialog.value();
        nativeAuthBusy_=true;startAuthProgress("Выполняем вход…");
        nativeAuth_->request("login",body,[this,login,openAfterLogin](const QVariantMap &result){
            finishAuthProgress();
            if(!result.value("authenticated").toBool()){showAuthFailure(result);return;}
            if(!web_->session()->markAuthenticated())logLine("session persistence failed after login");
            QSettings prefs("/mnt/ext1/litres-client/preferences.ini",QSettings::IniFormat);prefs.setValue("account/login",login);prefs.sync();
            setCurrentWidget(home_);focusFirstHomeButton();
            if(nativeAuth_){nativeAuth_->deleteLater();nativeAuth_=nullptr;}
            logLine("native auth succeeded; shared browser cookies ready");
            if(openAfterLogin)openUrl(kMyBooks);
            else QMessageBox::information(this,"Литрес","Вход выполнен. Теперь откройте «Литрес» в главном меню.");
        });
    }
    void beginNativeLogin(bool openAfterLogin=false) {
        if(nativeAuthBusy_)return;
        QSettings prefs("/mnt/ext1/litres-client/preferences.ini",QSettings::IniFormat);
        TextInputDialog loginDialog(prefs.value("account/login").toString(),false,this,"Вход в Литрес · логин или почта");
        if(loginDialog.exec()!=QDialog::Accepted)return;
        const QString login=loginDialog.value().trimmed();if(login.isEmpty())return;
        if(!ensurePocketBookNetwork()){QVariantMap result;result["networkError"]=true;showAuthFailure(result);return;}
        ensureWeb();
        if(!nativeAuth_)nativeAuth_=new NativeAuth(web_->page()->profile(),this);
        nativeAuthBusy_=true;startAuthProgress("Проверяем учётную запись…");
        web_->whenSessionReady([this,login,openAfterLogin](bool restored){
         if(!restored){finishAuthProgress();QVariantMap result;result["networkError"]=true;showAuthFailure(result);return;}
         nativeAuth_->initialize([this,login,openAfterLogin](bool ready){
            if(!ready){finishAuthProgress();QVariantMap result;result["networkError"]=true;showAuthFailure(result);return;}
            QJsonObject body;body["login"]=login;
            nativeAuth_->request("available",body,[this,login,openAfterLogin](const QVariantMap &result){
                finishAuthProgress();
                if(!result.value("ok").toBool() || !result.contains("available")){showAuthFailure(result);return;}
                if(result.value("available").toBool()) {
                    QMessageBox::information(this,"Вход в Литрес","Учётная запись не найдена. Проверьте логин или почту.");return;
                }
                requestNativePassword(login,openAfterLogin);
            });
         });
        });
    }
    QPointer<ReaderTransport> historyTransport_;
    bool openingLitres_=false;
    void openOfflineBooks(){const QString user=OfflineAccount::active("/mnt/ext1/litres-client");if(!ReaderHistory::numeric(user)){QMessageBox::information(this,"Офлайн","Сначала откройте книгу с Wi-Fi, чтобы сохранить её для чтения без интернета.");return;}OfflineLibrary books("/mnt/ext1/litres-client",user,this);int result=books.exec();if(result==QDialog::Accepted&&!books.art().isEmpty())openNativeReader(QUrl("https://www.litres.ru/static/reader/text/index.html?art="+books.art()),true);else if(result==OfflineLibrary::Website||result==QDialog::Rejected)leaveReaderToLibrary();else{setCurrentWidget(home_);focusFirstHomeButton();}}
    void chooseOfflineBook(){auto last=ReaderHistory::read("/mnt/ext1/litres-client",OfflineAccount::active("/mnt/ext1/litres-client"));setCurrentWidget(home_);if(!last.isEmpty()){QMessageBox box(QMessageBox::Question,"Продолжить чтение?",QString("Нет интернета. Продолжить читать «%1» с места, сохранённого на книге?").arg(last["title"].toString()),QMessageBox::NoButton,this);box.setTextFormat(Qt::PlainText);auto *yes=box.addButton("Да",QMessageBox::YesRole);box.addButton("Мои книги",QMessageBox::NoRole);box.setDefaultButton(yes);box.exec();if(box.clickedButton()==yes){openNativeReader(QUrl("https://www.litres.ru/static/reader/text/index.html?art="+last["art"].toString()),true);return;}}openOfflineBooks();}

    void openLitres(){
        if(openingLitres_)return;
        const bool stored=SessionPersistence::hasStoredSession("/mnt/ext1/litres-client");
        ensureWeb();
        if(!stored){beginNativeLogin(true);return;}
        if(!(QueryNetwork()&NET_CONNECTED)){chooseOfflineBook();return;}
        if(!ensurePocketBookNetwork()){chooseOfflineBook();return;}
        openingLitres_=true;setCurrentWidget(loading_);
        web_->whenSessionReady([this](bool ready){
            if(!ready){openingLitres_=false;setCurrentWidget(home_);focusFirstHomeButton();return;}
            auto *transport=new ReaderTransport(web_->page()->profile(),this);historyTransport_=transport;
            transport->request(QUrl("https://api.litres.ru/foundation/api/users/me"),"GET",{},[this,transport](int status,const QString &text){
                transport->deleteLater();auto response=QJsonDocument::fromJson(text.toUtf8()).object();auto user=response["payload"].toObject()["data"].toObject();
                const QString uid=QString::number(qint64(user["id"].toDouble()));
                if(status==0||status>=500){openingLitres_=false;chooseOfflineBook();return;}
                if(status==200&&response["error"].isNull()&&ReaderHistory::numeric(uid))OfflineAccount::remember("/mnt/ext1/litres-client",uid);
                auto last=status==200&&response["error"].isNull()?ReaderHistory::read("/mnt/ext1/litres-client",uid):QJsonObject();
                setCurrentWidget(home_);
                if(!last.isEmpty()){
                    QMessageBox box(QMessageBox::Question,"Продолжить чтение?",QString("Продолжить читать «%1»?\n\nМесто чтения будет проверено в Литрес.").arg(last["title"].toString()),QMessageBox::NoButton,this);
                    box.setTextFormat(Qt::PlainText);auto *yes=box.addButton("Да",QMessageBox::YesRole);box.addButton("Нет",QMessageBox::NoRole);box.setDefaultButton(yes);box.exec();openingLitres_=false;
                    if(box.clickedButton()==yes){openNativeReader(QUrl("https://www.litres.ru/static/reader/text/index.html?art="+last["art"].toString()));return;}
                }
                openingLitres_=false;openUrl(kMyBooks);
            });
        });
    }
    void logout(){
        if(QMessageBox::question(this,"Выход из аккаунта","Выйти из аккаунта Литрес на этой книге?")!=QMessageBox::Yes)return;
        if(reader_){removeWidget(reader_);delete reader_;reader_=nullptr;}
        QDir("/mnt/ext1/litres-client/native-reader").removeRecursively();
        if(nativeAuth_){nativeAuth_->cancel();delete nativeAuth_;nativeAuth_=nullptr;}finishAuthProgress();
        bool cleared=web_?web_->session()->clearForLogout():SessionPersistence::clearStoredSession("/mnt/ext1/litres-client");
        if(!cleared){QMessageBox::information(this,"Выход","Не удалось сохранить выход из аккаунта. Повторите попытку.");return;}
        setCurrentWidget(home_);focusFirstHomeButton();
        if(browserShell_){web_->stop();removeWidget(browserShell_);delete browserShell_;browserShell_=nullptr;web_=nullptr;}
        SessionPersistence::prepareProfile("/mnt/ext1/litres-client");
        QSettings p("/mnt/ext1/litres-client/preferences.ini",QSettings::IniFormat);p.remove("account");p.sync();
        logLine("explicit logout completed; preferences retained");
    }
    Home *home_ = nullptr;
    LitresWebView *web_ = nullptr;
    BrowserShell *browserShell_ = nullptr;
    LoadingScreen *loading_ = nullptr;
    SettingsScreen *settings_ = nullptr;
    QWidget *diagnostics_ = nullptr;
    QLabel *diagnosticsText_ = nullptr;

    static bool hasPocketCode(const QKeyEvent *event, quint32 code)
    {
        return quint32(event->key()) == code ||
               event->nativeVirtualKey() == code ||
               event->nativeScanCode() == code;
    }

    QList<QPushButton *> homeButtons() const
    {
        return home_ ? home_->findChildren<QPushButton *>()
                     : QList<QPushButton *>();
    }

    void focusFirstHomeButton()
    {
        const auto buttons = homeButtons();
        if (!buttons.isEmpty()) {
            buttons.first()->setFocus(Qt::OtherFocusReason);
            logLine(QString("focus home button 0: %1")
                    .arg(buttons.first()->text()));
        }
    }

    void moveHomeFocus(int delta)
    {
        const auto buttons = homeButtons();
        if (buttons.isEmpty())
            return;

        QWidget *focused = QApplication::focusWidget();
        int index = -1;

        for (int i = 0; i < buttons.size(); ++i) {
            if (buttons.at(i) == focused) {
                index = i;
                break;
            }
        }

        if (index < 0)
            index = 0;
        else
            index = (index + delta + buttons.size()) % buttons.size();

        buttons.at(index)->setFocus(Qt::OtherFocusReason);
        logLine(QString("focus home button %1: %2")
                .arg(index)
                .arg(buttons.at(index)->text()));
    }

    QWidget *createDiagnostics()
    {
        auto *page = new QWidget;
        page->setAutoFillBackground(true);
        page->setStyleSheet("background: white; color: black;");
        auto *layout = new QVBoxLayout(page);
        layout->setContentsMargins(30, 30, 30, 30);

        auto *title = new QLabel("Диагностика");
        QFont f = title->font();
        f.setPointSize(18);
        f.setBold(true);
        title->setFont(f);
        layout->addWidget(title);

        diagnosticsText_ = new QLabel;
        diagnosticsText_->setWordWrap(true);
        QFont body = diagnosticsText_->font();
        body.setPointSize(10);
        diagnosticsText_->setFont(body);
        layout->addWidget(diagnosticsText_);
        layout->addStretch();

        return page;
    }

    void refreshDiagnostics()
    {
        QFile f("/mnt/ext1/litres-client/litres.log");
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            diagnosticsText_->setText(
                "Лог пока пуст.\n\n"
                "/mnt/ext1/litres-client/litres.log");
            return;
        }

        QByteArray content = f.readAll();
        if (content.size() > 5000)
            content = content.right(5000);

        diagnosticsText_->setText(
            QString::fromUtf8(content));
    }

    void ensureWeb()
    {
        if (web_)
            return;

        logLine("creating WebEngine view");
        web_ = new LitresWebView;
        web_->setStyleSheet("background: white; color: black;");
        web_->backToMenu =
            [this]() {
                setCurrentWidget(home_);
                focusFirstHomeButton();
                logLine("back to home");
            };

        static_cast<DiagnosticWebPage *>(web_->page())->navigation=[this](const QUrl &url){
            if(url.scheme()=="https"&&url.host()=="www.litres.ru"&&url.path()=="/static/reader/text/index.html"){
                if(bypassNativeReader_)return true;
                QTimer::singleShot(0,this,[this,url](){openNativeReader(url);});return false;
            }
            bypassNativeReader_=false;return true;
        };
        browserShell_ = new BrowserShell(web_);
        addWidget(browserShell_);

        connect(web_, &QWebEngineView::urlChanged,
                this, [this](const QUrl &u) {
            if (!browserShell_)
                return;
            const QString s = u.toString();
            if (s.contains("my-books"))
                browserShell_->setTitleText("Литрес · Мои книги");
            else if (s.contains("login") || s.contains("auth"))
                browserShell_->setTitleText("Литрес · Вход");
            else
                browserShell_->setTitleText("Литрес");
        });

        connect(web_, &QWebEngineView::loadStarted, this, [this](){browserShell_->setLoading(true);});
        connect(web_, &QWebEngineView::loadFinished, this, [this](bool ok) {
            browserShell_->setLoading(false);
            if(!web_->sessionReady())return;
            if (currentWidget() != loading_) return;
            setCurrentWidget(browserShell_);
            web_->setFocus(Qt::OtherFocusReason);
            if (!ok) browserShell_->setTitleText("Литрес · Ошибка загрузки");
        });
        logLine("WebEngine view created");
    }

    void openUrl(const char *url)
    {
        logLine(QString("open requested %1").arg(url));

        PostponeTimedPoweroff();
        const bool online = ensurePocketBookNetwork();

        setCurrentWidget(loading_);
        QApplication::processEvents();

        ensureWeb();
        logLine(QString("loading screen shown network=%1").arg(online ? "online" : "offline"));

        // Give PocketBook's network manager a brief moment to publish the
        // route/DNS state before Chromium evaluates connectivity.
        QTimer::singleShot(30000, this, [this]() {
            if (currentWidget() == loading_) {
                setCurrentWidget(browserShell_);
                web_->setFocus(Qt::OtherFocusReason);
                logLine("loading screen timeout -> show current page");
            }
        });
        const QString target = QString::fromUtf8(url);
        QTimer::singleShot(700, this, [this, target]() {
            logLine(QString("open %1").arg(target));
            web_->whenSessionReady([this,target](bool ready){if(!ready){setCurrentWidget(home_);focusFirstHomeButton();QMessageBox::information(this,"Литрес","Не удалось восстановить сохранённую сессию. Данные сохранены. Перезапустите приложение или выберите вход в настройках.");return;}web_->load(QUrl(target));});
        });
    }
};


static void setEnvIfExistingFile(const char *name,
                                 const QStringList &candidates)
{
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            qputenv(name, candidate.toUtf8());
            logLine(QString("env %1=%2")
                    .arg(QString::fromLatin1(name), candidate));
            return;
        }
    }
    logLine(QString("env %1 not found")
            .arg(QString::fromLatin1(name)));
}

static void setEnvIfExistingDir(const char *name,
                                const QStringList &candidates)
{
    for (const QString &candidate : candidates) {
        if (QDir(candidate).exists()) {
            qputenv(name, candidate.toUtf8());
            logLine(QString("env %1=%2")
                    .arg(QString::fromLatin1(name), candidate));
            return;
        }
    }
    logLine(QString("env %1 not found")
            .arg(QString::fromLatin1(name)));
}

int main(int argc, char **argv)
{
    QDir().mkpath("/mnt/ext1/litres-client");

    std::freopen(
        "/mnt/ext1/litres-client/qt-startup.log",
        "a", stderr);
    std::freopen(
        "/mnt/ext1/litres-client/qt-stdout.log",
        "a", stdout);

    // Pure Qt application. Do not call InitInkview(): the PocketBook
    // "pocketbook2" QPA plugin owns framebuffer/input integration itself.
    qputenv("QT_QPA_PLATFORM", "pocketbook2");
    qputenv("QT_PLUGIN_PATH", "/ebrmain/lib/qtplugins");
    qputenv("QTWEBENGINE_DISABLE_SANDBOX", "1");
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS",
        "--disable-gpu "
        "--disable-gpu-compositing "
        "--disable-smooth-scrolling "
        "--disable-threaded-scrolling "
        "--disable-features=TranslateUI");

    // Firmware layouts vary slightly. Pick the first existing WebEngine
    // runtime path and record it in litres.log.
    setEnvIfExistingFile("QTWEBENGINEPROCESS_PATH", {
        "/ebrmain/bin/QtWebEngineProcess",
        "/usr/bin/QtWebEngineProcess",
        "/ebrmain/libexec/QtWebEngineProcess",
        "/usr/lib/qt5/libexec/QtWebEngineProcess"
    });

    setEnvIfExistingDir("QTWEBENGINE_RESOURCES_PATH", {
        "/ebrmain/resources",
        "/ebrmain/lib/qt5/resources",
        "/usr/lib/qt5/resources",
        "/usr/share/qt5/resources"
    });

    setEnvIfExistingDir("QTWEBENGINE_LOCALES_PATH", {
        "/ebrmain/translations/qtwebengine_locales",
        "/ebrmain/resources/qtwebengine_locales",
        "/usr/share/qt5/translations/qtwebengine_locales",
        "/usr/lib/qt5/qtwebengine_locales"
    });

    CompatAssets::registerScheme();
    prepareBrowserTrust();
    logLine("before QApplication");

    QApplication app(argc, argv);
    const int fontId = QFontDatabase::addApplicationFontFromData(
        QByteArray(reinterpret_cast<const char *>(pbFontData), sizeof(pbFontData)));
    const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
    QFont uiFont(families.isEmpty() ? QStringLiteral("DejaVu Sans") : families.first());
    uiFont.setPixelSize(30); uiFont.setLetterSpacing(QFont::AbsoluteSpacing, 0);
    app.setFont(uiFont);
    logLine(QString("UI font=%1 embedded=%2").arg(uiFont.family()).arg(fontId));
    QCoreApplication::setApplicationName("LitRes PocketBook");
    QCoreApplication::setApplicationVersion("0.6.16");
    QPixmap appIcon;appIcon.loadFromData(pbLauncherIcon,sizeof(pbLauncherIcon),"PNG");app.setWindowIcon(QIcon(appIcon));
    if(QFileInfo(QCoreApplication::applicationFilePath()).absolutePath()=="/mnt/ext1/applications")logLine(LauncherAssets::install("/mnt/ext1",QCoreApplication::applicationFilePath())?"launcher RGB24 icon installed with file-path objects; Mac duplicate checked":"launcher setup incomplete; existing configuration preserved");
    logLine("LitRes Browser v0.6.16 screen geometry and library navigation fixes");

    logLine("QApplication created");

    MainWindow window;

    QSize size(758, 1024);
    if (QScreen *screen = QGuiApplication::primaryScreen()) {
        size = screen->geometry().size();
        logLine(QString("screen %1x%2")
                .arg(size.width()).arg(size.height()));
    } else {
        logLine("primaryScreen missing, using 758x1024");
    }

    int reservedPanel = 0;
    if (QScreen *screen = QGuiApplication::primaryScreen()) {
        reservedPanel = qMax(0, screen->geometry().height() - screen->availableGeometry().height());
    }
    // pocketbook2 reports a full framebuffer but the firmware panel can shift
    // the application down. Reserve that strip so the footer stays visible.
    reservedPanel = qMax(reservedPanel, PanelHeight());
    reservedPanel = qBound(0, reservedPanel, size.height()/4);
    window.setContentsMargins(0,0,0,reservedPanel);
    logLine(QString("reserved system panel=%1").arg(reservedPanel));
    window.resize(size);
    window.setWindowState(window.windowState() | Qt::WindowFullScreen);
    window.showFullScreen();
    window.raise();
    window.activateWindow();
    window.repaint();

    app.processEvents(QEventLoop::AllEvents, 100);

    logLine(QString("window shown visible=%1 active=%2 geometry=%3x%4")
            .arg(window.isVisible() ? "yes" : "no")
            .arg(window.isActiveWindow() ? "yes" : "no")
            .arg(window.width()).arg(window.height()));

    // Prove that the Qt event loop is actually running on the device and
    // force one more first-frame update after the QPA plugin settles.
    QTimer::singleShot(250, [&window]() {
        logLine("event loop alive");
        window.update();
        window.repaint();
    });

    QTimer::singleShot(1000, []() {
        logLine("event loop +1s");
    });

    const int result = app.exec();

    logLine(QString("exit rc=%1").arg(result));
    return result;
}
