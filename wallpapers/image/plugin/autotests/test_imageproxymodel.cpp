/*
    SPDX-FileCopyrightText: 2022 Fushan Wen <qydwhotmail@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <QDebug>
#include <QtTest>

#include <KIO/CopyJob>

#include "../model/imagelistmodel.h"
#include "../model/imageproxymodel.h"
#include "../model/packagelistmodel.h"

class ImageProxyModelTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();
    void cleanupTestCase();

    void testImageProxyModelIndexOf();
    void testImageProxyModelReload();
    void testImageProxyModelAddBackground();
    void testImageProxyModelRemoveBackground();
    void testImageProxyModelDirWatch();

private:
    QPointer<ImageProxyModel> m_model = nullptr;
    QPointer<QSignalSpy> m_countSpy = nullptr;
    QPointer<QSignalSpy> m_dataSpy = nullptr;

    QDir m_dataDir;
    QDir m_alternateDir;
    QString m_wallpaperPath;
    QString m_dummyWallpaperPath;
    QString m_packagePath;
    QString m_dummyPackagePath;
    int m_modelNum = 0;
    QSize m_targetSize;
};

void ImageProxyModelTest::initTestCase()
{
    m_dataDir = QDir(QFINDTESTDATA("testdata/default"));
    m_alternateDir = QDir(QFINDTESTDATA("testdata/alternate"));
    QVERIFY(!m_dataDir.isEmpty());
    QVERIFY(!m_alternateDir.isEmpty());

    m_wallpaperPath = m_dataDir.absoluteFilePath(QStringLiteral("wallpaper.jpg.jpg"));
    m_dummyWallpaperPath = m_alternateDir.absoluteFilePath(QStringLiteral("dummy.jpg"));

    m_packagePath = m_dataDir.absoluteFilePath(QStringLiteral("package"));
    m_dummyPackagePath = m_alternateDir.absoluteFilePath(QStringLiteral("dummy"));

    m_modelNum = 2;
    m_targetSize = QSize(1920, 1080);

    QStandardPaths::setTestModeEnabled(true);
}

void ImageProxyModelTest::init()
{
    m_model = new ImageProxyModel({m_dataDir.absolutePath()}, m_targetSize, this);
    m_countSpy = new QSignalSpy(m_model, &ImageProxyModel::countChanged);
    m_dataSpy = new QSignalSpy(m_model, &ImageProxyModel::dataChanged);

    QVERIFY(m_model->loading());

    // Test loading data
    for (int i = 0; i < m_modelNum; i++) {
        m_countSpy->wait(5 * 1000);

        if (m_countSpy->size() == m_modelNum) {
            break;
        }
    }
    QCOMPARE(m_countSpy->size(), m_modelNum);
    m_countSpy->clear();
    QCOMPARE(m_model->sourceModels().size(), m_modelNum);

    QVERIFY(!m_model->loading());

    QCOMPARE(m_model->rowCount(), 2); // wallpaper.jpg.jpg, package
    QCOMPARE(m_model->count(), 2);

    QCOMPARE(m_model->m_imageModel->count(), 1);
    QCOMPARE(m_model->m_packageModel->count(), 1);
}

void ImageProxyModelTest::cleanup()
{
    m_model->deleteLater();
    m_countSpy->deleteLater();
    m_dataSpy->deleteLater();
}

void ImageProxyModelTest::cleanupTestCase()
{
    const QString standardPath = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/wallpapers/");

    QDir(standardPath).removeRecursively();
}

void ImageProxyModelTest::testImageProxyModelIndexOf()
{
    QVERIFY(m_model->indexOf(m_wallpaperPath) >= 0);
    QVERIFY(m_model->indexOf(m_packagePath) >= 0);
    QVERIFY(m_model->indexOf(m_packagePath + QDir::separator()) >= 0);
    QCOMPARE(m_model->indexOf(m_dataDir.absoluteFilePath(QStringLiteral("brokenpackage") + QDir::separator())), -1);
}

void ImageProxyModelTest::testImageProxyModelReload()
{
    m_model->reload();

    for (int i = 0; i < m_modelNum; i++) {
        m_countSpy->wait(5 * 1000);
    }
    QCOMPARE(m_countSpy->size(), m_modelNum);
    m_countSpy->clear();

    QCOMPARE(m_model->rowCount(), 2);
    QCOMPARE(m_model->m_imageModel->rowCount(), 1);
    QCOMPARE(m_model->m_packageModel->rowCount(), 1);
}

void ImageProxyModelTest::testImageProxyModelAddBackground()
{
    int count = m_model->count();

    // Case 1: add an existing wallpaper
    auto results = m_model->addBackground(m_wallpaperPath);
    QCOMPARE(m_countSpy->size(), 0);
    QCOMPARE(results.size(), 0);

    // Case 2: add an existing package
    results = m_model->addBackground(m_packagePath);
    QCOMPARE(m_countSpy->size(), 0);
    QCOMPARE(results.size(), 0);

    // Case 3: add a new wallpaper
    results = m_model->addBackground(QUrl::fromLocalFile(m_dummyWallpaperPath).toString());
    QCOMPARE(m_countSpy->size(), 1);
    m_countSpy->clear();
    QCOMPARE(results.size(), 1);
    QCOMPARE(m_model->count(), ++count);
    QCOMPARE(m_model->m_imageModel->count(), 2);
    QCOMPARE(m_model->m_packageModel->count(), 1);

    // Case 4: add a new package
    results = m_model->addBackground(m_dummyPackagePath);
    QCOMPARE(m_countSpy->size(), 1);
    m_countSpy->clear();
    QCOMPARE(results.size(), 1);
    QCOMPARE(m_model->count(), ++count);
    QCOMPARE(m_model->m_imageModel->count(), 2);
    QCOMPARE(m_model->m_packageModel->count(), 2);

    // Test KDirWatch
    QVERIFY(m_model->m_dirWatch.contains(m_dummyWallpaperPath));
    QVERIFY(m_model->m_dirWatch.contains(m_dummyPackagePath));

    QCOMPARE(m_model->m_pendingAddition.size(), 2);
}

void ImageProxyModelTest::testImageProxyModelRemoveBackground()
{
    auto results = m_model->addBackground(m_dummyWallpaperPath);
    QCOMPARE(results.size(), 1);

    results = m_model->addBackground(m_dummyPackagePath);
    QCOMPARE(results.size(), 1);
    QCOMPARE(m_model->m_pendingAddition.size(), 2);
    m_countSpy->clear();

    int count = m_model->count();

    // Case 1: remove an existing wallpaper
    m_model->removeBackground(QUrl::fromLocalFile(m_dummyWallpaperPath).toString());
    QCOMPARE(m_countSpy->size(), 1);
    m_countSpy->clear();
    QCOMPARE(m_model->m_imageModel->count(), 1);
    QCOMPARE(m_model->m_packageModel->count(), 2);
    QCOMPARE(m_model->count(), --count);
    QVERIFY(!m_model->m_dirWatch.contains(m_dummyWallpaperPath));

    m_model->removeBackground(m_dummyPackagePath);
    QCOMPARE(m_countSpy->size(), 1);
    m_countSpy->clear();
    QCOMPARE(m_model->m_imageModel->count(), 1);
    QCOMPARE(m_model->m_packageModel->count(), 1);
    QCOMPARE(m_model->count(), --count);
    QVERIFY(!m_model->m_dirWatch.contains(m_dummyPackagePath));

    QCOMPARE(m_model->m_pendingAddition.size(), 0);

    // Case 2: remove an unexisting wallpaper
    m_model->removeBackground(m_dummyWallpaperPath);
    QCOMPARE(m_countSpy->size(), 0);
    QCOMPARE(m_model->count(), count);
}

void ImageProxyModelTest::testImageProxyModelDirWatch()
{
    QVERIFY(m_model->m_dirWatch.contains(m_dataDir.absolutePath()));
    QVERIFY(m_model->m_dirWatch.contains(m_wallpaperPath));
    QVERIFY(m_model->m_dirWatch.contains(m_packagePath));

    m_model->deleteLater();
    m_countSpy->deleteLater();
    m_dataSpy->deleteLater();

    // Move to local wallpaper folder
    const QString standardPath = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/wallpapers/");
    QVERIFY(QDir(standardPath).mkpath(standardPath));

    m_model = new ImageProxyModel({standardPath}, m_targetSize, this);
    m_countSpy = new QSignalSpy(m_model, &ImageProxyModel::countChanged);
    m_dataSpy = new QSignalSpy(m_model, &ImageProxyModel::dataChanged);

    QVERIFY(!m_countSpy->wait(1000));
    QCOMPARE(m_countSpy->size(), 0);
    QCOMPARE(m_model->sourceModels().size(), m_modelNum);
    QCOMPARE(m_model->rowCount(), 0);
    QVERIFY(m_model->m_dirWatch.contains(standardPath));

    // Copy an image to the folder
    QFile imageFile(m_wallpaperPath);
    QVERIFY(imageFile.copy(standardPath + QStringLiteral("image.jpg")));
    m_countSpy->wait();
    QCOMPARE(m_countSpy->size(), 1);
    m_countSpy->clear();

    QCOMPARE(m_model->count(), 1);
    QCOMPARE(m_model->m_imageModel->count(), 1);
    QCOMPARE(m_model->m_packageModel->count(), 0);
    QVERIFY(m_model->m_dirWatch.contains(standardPath + QStringLiteral("image.jpg")));

    // Copy a package to the folder
    auto job = KIO::copy(QUrl::fromLocalFile(m_dummyPackagePath),
                         QUrl::fromLocalFile(standardPath + QStringLiteral("dummy") + QDir::separator()),
                         KIO::HideProgressInfo | KIO::Overwrite);
    job->start();

    QVERIFY(m_countSpy->wait());
    QCOMPARE(m_countSpy->size(), 1);
    m_countSpy->clear();

    QCOMPARE(m_model->count(), 2);
    QCOMPARE(m_model->m_imageModel->count(), 1);
    QCOMPARE(m_model->m_packageModel->count(), 1);
    QVERIFY(m_model->m_dirWatch.contains(standardPath + QStringLiteral("dummy")));

    // Test delete a file
    QFile newImageFile(standardPath + QStringLiteral("image.jpg"));
    QVERIFY(newImageFile.remove());
    m_countSpy->wait();
    QCOMPARE(m_countSpy->size(), 1);
    m_countSpy->clear();

    QCOMPARE(m_model->count(), 1);
    QCOMPARE(m_model->m_imageModel->count(), 0);
    QCOMPARE(m_model->m_packageModel->count(), 1);
    QVERIFY(!m_model->m_dirWatch.contains(standardPath + QStringLiteral("image.jpg")));

    // Test delete a folder
    QVERIFY(QDir(standardPath + QStringLiteral("dummy")).removeRecursively());
    m_countSpy->wait();
    QCOMPARE(m_countSpy->size(), 1);
    m_countSpy->clear();

    QCOMPARE(m_model->count(), 0);
    QCOMPARE(m_model->m_imageModel->count(), 0);
    QCOMPARE(m_model->m_packageModel->count(), 0);
    QVERIFY(!m_model->m_dirWatch.contains(standardPath + QStringLiteral("dummy")));
}

QTEST_MAIN(ImageProxyModelTest)

#include "test_imageproxymodel.moc"