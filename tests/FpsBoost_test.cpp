// SPDX-License-Identifier: GPL-3.0-only
#include <QTest>
#include <QTemporaryDir>
#include "archive/ArchiveWriter.h"
#include "minecraft/fpsboost/FpsBoost.h"

class FpsBoostTest : public QObject {
    Q_OBJECT
private slots:
    void ranges_data();
    void ranges();
    void requiredAndIncompatible();
    void actualFabricJar();
    void actualForgeJar();
    void mappings();
};
void FpsBoostTest::ranges_data()
{
        QTest::addColumn<QString>("version");
        QTest::addColumn<QString>("range");
        QTest::addColumn<bool>("maven");
        QTest::addColumn<bool>("expected");
        QTest::newRow("fabric-ok") << "0.16.10" << ">=0.16.9" << false << true;
        QTest::newRow("fabric-too-old") << "0.15.11" << ">=0.16.9" << false << false;
        QTest::newRow("numeric-not-lexical") << "0.16.10" << ">0.16.9" << false << true;
        QTest::newRow("trailing-zero") << "0.16.0" << "=0.16" << false << true;
        QTest::newRow("and") << "0.16.10" << ">=0.16.9 <0.17" << false << true;
        QTest::newRow("and-reject") << "0.17" << ">=0.16.9 <0.17" << false << false;
        QTest::newRow("or") << "0.18" << "<0.15 || >=0.18" << false << true;
        QTest::newRow("wildcard") << "1.20.1" << "1.20.x" << false << true;
        QTest::newRow("wildcard-reject") << "1.21" << "1.20.*" << false << false;
        QTest::newRow("caret") << "0.16.9" << "^0.16.1" << false << true;
        QTest::newRow("caret-upper") << "0.17" << "^0.16.1" << false << false;
        QTest::newRow("tilde") << "1.20.6" << "~1.20.1" << false << true;
        QTest::newRow("tilde-upper") << "1.21" << "~1.20.1" << false << false;
        QTest::newRow("forge-lower") << "47.2.0" << "[47,)" << true << true;
        QTest::newRow("forge-too-old") << "46.0.1" << "[47,)" << true << false;
        QTest::newRow("neo-range") << "21.1.10" << "[21.1,21.2)" << true << true;
        QTest::newRow("neo-upper") << "21.2" << "[21.1,21.2)" << true << false;
        QTest::newRow("exact-maven") << "21.1.0" << "[21.1]" << true << true;
        QTest::newRow("union") << "3" << "(,1],[3,)" << true << true;
        QTest::newRow("union-hole") << "2" << "(,1],[3,)" << true << false;
        QTest::newRow("invalid") << "0.16" << "bananas >= 0.12" << false << false;
        QTest::newRow("invalid-maven") << "47.1" << "[47,) garbage" << true << false;
        QTest::newRow("release-after-snapshot") << "1.21.1" << ">=1.15-alpha.19.39.a" << false << true;
        QTest::newRow("release-after-prerelease") << "0.16" << ">=0.16-beta" << false << true;
        QTest::newRow("prerelease-order") << "1.0-beta.10" << ">1.0-beta.2" << false << true;
        QTest::newRow("release-upper-exclusive") << "21.1" << "[21.0,21.1)" << true << false;
        QTest::newRow("unknown-prerelease") << "0.16-beta" << ">=0.16" << false << false;
}

void FpsBoostTest::ranges()
{
        QFETCH(QString, version);
        QFETCH(QString, range);
        QFETCH(bool, maven);
        QFETCH(bool, expected);
        QCOMPARE(FpsBoost::matchesVersion(version, range, maven), expected);
}

void FpsBoostTest::requiredAndIncompatible()
{
        FpsBoost::Manifest mod;
        mod.provides.insert("renderer", "1.0");
        mod.requirements.append({"fabricloader", {">=0.16"}, false, false});
        mod.requirements.append({"minecraft", {"1.21.1"}, false, false});
        mod.requirements.append({"library", {">=2"}, false, false});
        FpsBoost::Manifest library;
        library.provides.insert("library", "2.1");
        QVERIFY(!FpsBoost::compatible({mod}, "1.21.1", "fabric", "0.16.10"));
        QVERIFY(FpsBoost::compatible({mod, library}, "1.21.1", "fabric", "0.16.10"));
        QVERIFY(!FpsBoost::compatible({mod, library}, "1.21.1", "fabric", "0.15.11"));
        QVERIFY(!FpsBoost::compatible({mod, library}, "1.20.1", "fabric", "0.16.10"));
        QVERIFY(!FpsBoost::compatible({mod, library}, "1.21.1", "forge", "47.2"));
        mod.requirements.append({"library", {">=2"}, true, false});
        QVERIFY(!FpsBoost::compatible({mod, library}, "1.21.1", "fabric", "0.16.10"));
        mod.requirements.last().alternatives = {"unknown-range-syntax"};
        QVERIFY(!FpsBoost::compatible({mod, library}, "1.21.1", "fabric", "0.16.10"));
}

void FpsBoostTest::actualFabricJar()
{
        QTemporaryDir dir;
        const auto path = dir.filePath("test.jar");
        MMCZip::ArchiveWriter archive(path);
        QVERIFY(archive.open());
        QVERIFY(archive.addFile(QString("fabric.mod.json"), QByteArray(R"({"schemaVersion":1,"id":"testmod","version":"1.0","depends":{"fabricloader":">=0.16.9","minecraft":"1.21.1"}})")));
        QVERIFY(archive.close());
        FpsBoost::Manifest result;
        QVERIFY(FpsBoost::readManifest(path, "fabric", result));
        QCOMPARE(result.provides.value("testmod"), QString("1.0"));
        QVERIFY(FpsBoost::compatible({result}, "1.21.1", "fabric", "0.16.10"));
        QVERIFY(!FpsBoost::compatible({result}, "1.21.1", "fabric", "0.15.11"));
        QVERIFY(!FpsBoost::readManifest(path, "forge", result));
}

void FpsBoostTest::actualForgeJar()
{
        QTemporaryDir dir;
        const auto path = dir.filePath("forge.jar");
        MMCZip::ArchiveWriter archive(path);
        QVERIFY(archive.open());
        QVERIFY(archive.addFile(QString("META-INF/mods.toml"), QByteArray(R"TOML(
modLoader="javafml"
loaderVersion="[47,)"
[[mods]]
modId="testmod"
version="1.0"
[[dependencies.testmod]]
modId="forge"
mandatory=true
versionRange="[47.2,)"
[[dependencies.testmod]]
modId="minecraft"
mandatory=true
versionRange="[1.20.1]"
)TOML")));
        QVERIFY(archive.close());
        FpsBoost::Manifest result;
        QVERIFY(FpsBoost::readManifest(path, "forge", result));
        QVERIFY(FpsBoost::compatible({result}, "1.20.1", "forge", "47.2.0"));
        QVERIFY(!FpsBoost::compatible({result}, "1.20.1", "forge", "47.1.0"));
        QVERIFY(!FpsBoost::compatible({result}, "1.20.2", "forge", "47.2.0"));
}

void FpsBoostTest::mappings()
{
        QCOMPARE(FpsBoost::apiLoader("net.neoforged"), QString("neoforge"));
        QCOMPARE(FpsBoost::apiLoader("net.minecraftforge"), QString("forge"));
        QCOMPARE(FpsBoost::apiLoader("net.fabricmc.fabric-loader"), QString("fabric"));
        QVERIFY(FpsBoost::apiLoader("com.mumfrey.liteloader").isEmpty());
        QVERIFY(FpsBoost::apiLoader("").isEmpty());
}

QTEST_GUILESS_MAIN(FpsBoostTest)
#include "FpsBoost_test.moc"
