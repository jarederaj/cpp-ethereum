#include <QtNetwork/QNetworkReply>
#include <QtQuick/QQuickView>
#include <QtQml/QQmlContext>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QInputDialog>
#include <QtGui/QClipboard>
#include <QtCore/QtCore>
#include <libethereum/Dagger.h>
#include <libethereum/Client.h>
#include <libethereum/Instruction.h>
#include "MainWin.h"
#include "ui_Main.h"
using namespace std;

// types
using eth::bytes;
using eth::bytesConstRef;
using eth::h160;
using eth::h256;
using eth::u160;
using eth::u256;
using eth::Address;
using eth::BlockInfo;
using eth::Client;
using eth::Instruction;
using eth::KeyPair;
using eth::NodeMode;
using eth::PeerInfo;
using eth::RLP;
using eth::Secret;
using eth::Transaction;

// functions
using eth::asHex;
using eth::assemble;
using eth::compileLisp;
using eth::disassemble;
using eth::formatBalance;
using eth::fromUserHex;
using eth::right160;
using eth::simpleDebugOut;
using eth::toLog2;
using eth::toString;
using eth::units;

// vars
using eth::g_logPost;
using eth::g_logVerbosity;
using eth::c_instructionInfo;

#define ADD_QUOTES_HELPER(s) #s
#define ADD_QUOTES(s) ADD_QUOTES_HELPER(s)

Main::Main(QWidget *parent) :
	QMainWindow(parent),
	ui(new Ui::Main)
{
	setWindowFlags(Qt::Window);
	ui->setupUi(this);
	m_client.reset(new Client("Walleth"));

	/*
	ui->librariesView->setModel(m_libraryMan);
	ui->graphsView->setModel(m_graphMan);
	setWindowIcon(QIcon(":/Noted.png"));

	qmlRegisterSingletonType<TimeHelper>("com.llr", 1, 0, "Time", TimelineItem::constructTimeHelper);
	qmlRegisterType<GraphItem>("com.llr", 1, 0, "Graph");
	qmlRegisterType<CursorGraphItem>("com.llr", 1, 0, "CursorGraph");
	qmlRegisterType<IntervalItem>("com.llr", 1, 0, "Interval");
	qmlRegisterType<CursorItem>("com.llr", 1, 0, "Cursor");
	qmlRegisterType<TimelinesItem>("com.llr", 1, 0, "Timelines");
	qmlRegisterType<TimeLabelsItem>("com.llr", 1, 0, "TimeLabels");
	qmlRegisterType<XLabelsItem>("com.llr", 1, 0, "XLabels");
	qmlRegisterType<XScaleItem>("com.llr", 1, 0, "XScale");
	qmlRegisterType<YLabelsItem>("com.llr", 1, 0, "YLabels");
	qmlRegisterType<YScaleItem>("com.llr", 1, 0, "YScale");
*/
	m_view = new QQuickView();
	QQmlContext* context = m_view->rootContext();
//	context->setContextProperty("eth", m_eth);
/*	context->setContextProperty("compute", compute());
	context->setContextProperty("data", data());
	context->setContextProperty("graphs", graphs());
	context->setContextProperty("audio", audio());
	context->setContextProperty("view", view());
*/	m_view->setSource(QUrl("qrc:/Simple.qml"));


	QWidget* w = QWidget::createWindowContainer(m_view);
	m_view->setResizeMode(QQuickView::SizeRootObjectToView);
	ui->fullDisplay->insertWidget(0, w);
	m_view->create();
/*
	m_timelinesItem = m_view->rootObject()->findChild<TimelinesItem*>("timelines");
	qDebug() << m_view->rootObject();
	*/

	readSettings();
	refresh();

	m_refresh = new QTimer(this);
	connect(m_refresh, SIGNAL(timeout()), SLOT(refresh()));
	m_refresh->start(100);
	m_refreshNetwork = new QTimer(this);
	connect(m_refreshNetwork, SIGNAL(timeout()), SLOT(refreshNetwork()));
	m_refreshNetwork->start(1000);

	connect(&m_webCtrl, &QNetworkAccessManager::finished, [&](QNetworkReply* _r)
	{
		m_servers = QString::fromUtf8(_r->readAll()).split("\n", QString::SkipEmptyParts);
		if (m_servers.size())
		{
			ui->net->setChecked(true);
			on_net_triggered(true);
		}
	});
	QNetworkRequest r(QUrl("http://www.ethereum.org/servers.poc3.txt"));
	r.setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (Windows NT 6.3; WOW64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/33.0.1712.0 Safari/537.36");
	m_webCtrl.get(r);
	srand(time(0));

	statusBar()->addPermanentWidget(ui->balance);
	statusBar()->addPermanentWidget(ui->peerCount);
	statusBar()->addPermanentWidget(ui->blockCount);
}

Main::~Main()
{
	writeSettings();
}

void Main::on_about_triggered()
{
	QMessageBox::about(this, "About Walleth PoC-" + QString(ADD_QUOTES(ETH_VERSION)).section('.', 1, 1), "AlethZero/v" ADD_QUOTES(ETH_VERSION) "/" ADD_QUOTES(ETH_BUILD_TYPE) "/" ADD_QUOTES(ETH_BUILD_PLATFORM) "\nBy Gav Wood, 2014.\nBased on a design by Vitalik Buterin.\n\nTeam Ethereum++ includes: Tim Hughes, Eric Lombrozo, Marko Simovic, Alex Leverington and several others.");
}

void Main::writeSettings()
{
	QSettings s("ethereum", "walleth");
	QByteArray b;
	b.resize(sizeof(Secret) * m_myKeys.size());
	auto p = b.data();
	for (auto i: m_myKeys)
	{
		memcpy(p, &(i.secret()), sizeof(Secret));
		p += sizeof(Secret);
	}
	s.setValue("address", b);

	s.setValue("upnp", ui->upnp->isChecked());
	s.setValue("clientName", m_clientName);
	s.setValue("idealPeers", m_idealPeers);
	s.setValue("port", m_port);

	if (m_client->peerServer())
	{
		bytes d = m_client->peerServer()->savePeers();
		m_peers = QByteArray((char*)d.data(), (int)d.size());

	}
	s.setValue("peers", m_peers);

	s.setValue("geometry", saveGeometry());
	s.setValue("windowState", saveState());
}

void Main::readSettings()
{
	QSettings s("ethereum", "walleth");

	restoreGeometry(s.value("geometry").toByteArray());
	restoreState(s.value("windowState").toByteArray());


	QByteArray b = s.value("address").toByteArray();
	if (b.isEmpty())
		m_myKeys.append(KeyPair::create());
	else
	{
		h256 k;
		for (unsigned i = 0; i < b.size() / sizeof(Secret); ++i)
		{
			memcpy(&k, b.data() + i * sizeof(Secret), sizeof(Secret));
			m_myKeys.append(KeyPair(k));
		}
	}
	m_client->setAddress(m_myKeys.back().address());
	m_peers = s.value("peers").toByteArray();
	ui->upnp->setChecked(s.value("upnp", true).toBool());
	m_clientName = s.value("clientName", "").toString();
	m_idealPeers = s.value("idealPeers", 5).toInt();
	m_port = s.value("port", 30303).toInt();
}

void Main::refreshNetwork()
{
	auto ps = m_client->peers();
	ui->peerCount->setText(QString::fromStdString(toString(ps.size())) + " peer(s)");
}

eth::State const& Main::state() const
{
	return ui->preview->isChecked() ? m_client->postState() : m_client->state();
}

void Main::refresh(bool _override)
{
	m_client->lock();
	auto const& st = state();

	bool c = m_client->changed();
	if (c || _override)
	{
		auto d = m_client->blockChain().details();
		auto diff = BlockInfo(m_client->blockChain().block()).difficulty;
		ui->blockCount->setText(QString("#%1 @%3 T%2").arg(d.number).arg(toLog2(d.totalDifficulty)).arg(toLog2(diff)));
	}

	if (c || m_keysChanged || _override)
	{
		m_keysChanged = false;
		u256 totalBalance = 0;
		for (auto i: m_myKeys)
		{
			u256 b = st.balance(i.address());
			totalBalance += b;
		}
		ui->balance->setText(QString::fromStdString(formatBalance(totalBalance)));
	}
	m_client->unlock();
}

void Main::on_net_triggered(bool _auto)
{
	string n = "Walleth/v" ADD_QUOTES(ETH_VERSION);
	if (m_clientName.size())
		n += "/" + m_clientName.toStdString();
	n +=  "/" ADD_QUOTES(ETH_BUILD_TYPE) "/" ADD_QUOTES(ETH_BUILD_PLATFORM);
	m_client->setClientVersion(n);
	if (ui->net->isChecked())
	{
		if (_auto)
		{
			QString s = m_servers[rand() % m_servers.size()];
			m_client->startNetwork(m_port, s.section(':', 0, 0).toStdString(), s.section(':', 1).toInt(), NodeMode::Full, m_idealPeers, std::string(), ui->upnp->isChecked());
		}
		else
			m_client->startNetwork(m_port, string(), 0, NodeMode::Full, m_idealPeers, std::string(), ui->upnp->isChecked());
		if (m_peers.size())
			m_client->peerServer()->restorePeers(bytesConstRef((byte*)m_peers.data(), m_peers.size()));
	}
	else
		m_client->stopNetwork();
}

void Main::on_connect_triggered()
{
	if (!ui->net->isChecked())
	{
		ui->net->setChecked(true);
		on_net_triggered();
	}
	bool ok = false;
	QString s = QInputDialog::getItem(this, "Connect to a Network Peer", "Enter a peer to which a connection may be made:", m_servers, m_servers.count() ? rand() % m_servers.count() : 0, true, &ok);
	if (ok && s.contains(":"))
	{
		string host = s.section(":", 0, 0).toStdString();
		unsigned short port = s.section(":", 1).toInt();
		m_client->connect(host, port);
	}
}

void Main::on_mine_triggered()
{
	if (ui->mine->isChecked())
	{
		m_client->setAddress(m_myKeys.last().address());
		m_client->startMining();
	}
	else
		m_client->stopMining();
}

void Main::on_create_triggered()
{
	m_myKeys.append(KeyPair::create());
	m_keysChanged = true;
}

// extra bits needed to link on VS
#ifdef _MSC_VER

// include moc file, ofuscated to hide from automoc
#include\
"moc_MainWin.cpp"

// specify library dependencies, it's easier to do here than in the project since we can control the "d" debug suffix
#ifdef _DEBUG
#define QTLIB(x) x"d.lib"
#else 
#define QTLIB(x) x".lib"
#endif

#pragma comment(lib, QTLIB("Qt5PlatformSupport"))
#pragma comment(lib, QTLIB("Qt5Core"))
#pragma comment(lib, QTLIB("Qt5GUI"))
#pragma comment(lib, QTLIB("Qt5Widgets"))
#pragma comment(lib, QTLIB("Qt5Network"))
#pragma comment(lib, QTLIB("qwindows"))
#pragma comment(lib, "Imm32.lib")
#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "winmm.lib")


#endif