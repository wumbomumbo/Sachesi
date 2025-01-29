// Copyright (C) 2014 Sacha Refshauge

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 3.0.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 3.0 for more details.

// A copy of the GPL 3.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official GIT repository and contact information can be found at
// http://github.com/xsacha/Sachesi

#pragma once

#include <QtNetwork>
#include <QObject>
#include "apps.h"
#include "splitter.h"
#include "downloadinfo.h"

#ifdef BLACKBERRY
class InstallNet;
#else
#include "installer.h"
#endif

enum SplitType {
    SplittingIdle = 0,
    SplittingAuto = 1,
    CreatingAuto = 2,
    ExtractingImage = 3,
    ExtractingApps = 4,
    FetchingCap = 5,
};

class MainNet : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString softwareRelease MEMBER _softwareRelease NOTIFY softwareReleaseChanged) // from reverse lookup
    Q_PROPERTY(QString updateMessage MEMBER _updateMessage NOTIFY updateMessageChanged)
    Q_PROPERTY(QQmlListProperty<Apps> updateAppList READ updateAppList NOTIFY updateMessageChanged)
    Q_PROPERTY(int     updateAppCount READ updateAppCount NOTIFY updateMessageChanged)
    Q_PROPERTY(int     updateCheckedCount READ updateCheckedCount NOTIFY updateCheckedCountChanged)
    Q_PROPERTY(int     updateAppNeededCount READ updateAppNeededCount NOTIFY updateMessageChanged)
    Q_PROPERTY(int     updateCheckedNeededCount READ updateCheckedNeededCount NOTIFY updateCheckedCountChanged)
    Q_PROPERTY(QString error MEMBER _error NOTIFY errorChanged)
    Q_PROPERTY(QString multiscanVersion MEMBER _multiscanVersion NOTIFY updateMessageChanged)
    Q_PROPERTY(bool    hasBootAccess READ hasBootAccess CONSTANT)
    Q_PROPERTY(bool    multiscan MEMBER _multiscan WRITE setMultiscan NOTIFY multiscanChanged)
    Q_PROPERTY(int     scanning MEMBER _scanning WRITE setScanning NOTIFY scanningChanged)
    Q_PROPERTY(int     maxId MEMBER _maxId NOTIFY maxIdChanged)
    Q_PROPERTY(int     splitting MEMBER _splitting NOTIFY splittingChanged)
    Q_PROPERTY(int     splitProgress MEMBER _splitProgress WRITE setSplitProgress NOTIFY splitProgressChanged)

public:
    MainNet(InstallNet* installer = nullptr, QObject* parent = 0);
    ~MainNet();
    Q_INVOKABLE void updateDetailRequest(QString delta, QString carrier, QString country, int device, int variant, int mode/*, int server, int version*/);
    Q_INVOKABLE void createRCFSImageFromFolder(const QUrl &folderUrl, const QString &outputPath);
    Q_INVOKABLE void decompressRCFS(const QUrl &fileUrl, const QString &outputPath);
    Q_INVOKABLE void downloadLinks(int downloadDevice = 0);
    Q_INVOKABLE void splitAutoloader(QUrl, int options);
    Q_INVOKABLE void combineAutoloader(QList<QUrl> selectedFiles);
    Q_INVOKABLE void extractImage(int type, int options);
    Q_INVOKABLE void grabLinks(int downloadDevice);
    Q_INVOKABLE void abortSplit();
    void splitConnectStart();

    Q_INVOKABLE QString nameFromVariant(unsigned int device, unsigned int variant);
    Q_INVOKABLE QString hwidFromVariant(unsigned int device, unsigned int variant);
    Q_INVOKABLE unsigned int variantCount(unsigned int device);
    bool    hasBootAccess()  const { return
#ifdef BOOTLOADER_ACCESS
                true;
#else
                false;
#endif
                                   }
    void    setMultiscan(const bool &multiscan);
    void    setScanning(const int &scanning);
    QQmlListProperty<Apps> updateAppList() {
        return QQmlListProperty<Apps>(this, _updateAppList);
    }

    int updateAppCount() const { return _updateAppList.count(); }
    int updateAppNeededCount() const {
        int count = 0;
        foreach (Apps* app, _updateAppList) {
            if (app->isAvailable() && !app->isInstalled())
                count++;
        }
        return count;
    }
    int updateCheckedCount() const {
        int checked = 0;
        foreach (Apps* app, _updateAppList)
            if (app->isMarked())
                checked++;
        return checked;
    }
    int updateCheckedNeededCount() const {
        int checked = 0;
        foreach (Apps* app, _updateAppList)
            if (app->isMarked() && (app->isAvailable() && !app->isInstalled()))
                checked++;
        return checked;
    }
    DownloadInfo* currentDownload;
public slots:
    void setSplitProgress(const int &progress);
    void newDeviceConnected();
signals:
    void softwareReleaseChanged();
    void updateMessageChanged();
    void updateCheckedCountChanged();
    void errorChanged();
    void multiscanChanged();
    void scanningChanged();
    void hasBootAccessChanged();
    void maxIdChanged();
    void splittingChanged();
    void splitProgressChanged();
private slots:
    void serverReply();
    void showFirmwareData(QByteArray data, QString variant);
    void serverError(QNetworkReply::NetworkError error);
    void cancelSplit();
// Blackberry
	void extractImageSlot(const QStringList& selectedFiles);

private:
    // Utils:
    InstallNet* _i;
    void verifyLink(QString url, QString type);
    QString convertLinks(QString prepend);
    QString fixVariantName(QString name, QString replace, int type);
    void fixApps();
    QString NPCFromLocale(int country, int carrier);

    QThread* splitThread;
    Splitter* splitter;
    QNetworkAccessManager *manager;
    QList<Apps*> _updateAppList;
    QString _updateMessage;
    QString _softwareRelease;
    QString _versionRelease;
    QString _error;
    QString _multiscanVersion;
    bool _multiscan;
    int _scanning;
    QFile _currentFile;
    int _maxId, _dlBytes, _dlTotal;
    int _splitting, _splitProgress;
    int _options;
    int _type;
    int _downloadDevice;
};
