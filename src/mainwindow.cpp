#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "ui_about.h"
#include "ui_settings.h"
#include "rpc.h"
#include "balancestablemodel.h"
#include "settings.h"
#include "utils.h"

#include "precompiled.h"


using json = nlohmann::json;

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // Status Bar
    loadingLabel = new QLabel();
    loadingMovie = new QMovie(":/icons/res/loading.gif");
    loadingMovie->setScaledSize(QSize(32, 16));
    loadingMovie->start();
    loadingLabel->setAttribute(Qt::WA_NoSystemBackground);
    loadingLabel->setMovie(loadingMovie);
        
    ui->statusBar->addPermanentWidget(loadingLabel);
    loadingLabel->setVisible(false);    

	ui->statusBar->setContextMenuPolicy(Qt::CustomContextMenu);
	QObject::connect(ui->statusBar, &QStatusBar::customContextMenuRequested, [=](QPoint pos) {
		auto msg = ui->statusBar->currentMessage();
		if (!msg.isEmpty() && msg.startsWith(Utils::txidStatusMessage)) {
			QMenu menu(this);
			menu.addAction("Copy txid", [=]() {
				QGuiApplication::clipboard()->setText(msg.split(":")[1].trimmed());
			});
			QPoint gpos(mapToGlobal(pos).x(), mapToGlobal(pos).y() + this->height() - ui->statusBar->height());
			menu.exec(gpos);
		}
		
	});

    statusLabel = new QLabel();
    ui->statusBar->addPermanentWidget(statusLabel);

    statusIcon = new QLabel();
    ui->statusBar->addPermanentWidget(statusIcon);
    
	// Set up File -> Settings action
	QObject::connect(ui->actionSettings, &QAction::triggered, [=]() {
		QDialog settingsDialog(this);
		Ui_Settings settings;
		settings.setupUi(&settingsDialog);

		QIntValidator validator(0, 65535);
		settings.port->setValidator(&validator);

		// Load previous values into the dialog		
		settings.hostname	->setText(Settings::getInstance()->getHost());
		settings.port		->setText(Settings::getInstance()->getPort());
		settings.rpcuser	->setText(Settings::getInstance()->getUsernamePassword().split(":")[0]);
		settings.rpcpassword->setText(Settings::getInstance()->getUsernamePassword().split(":")[1]);
		
		if (settingsDialog.exec() == QDialog::Accepted) {
			// Save settings
			QSettings s;
			s.setValue("connection/host",           settings.hostname->text());
			s.setValue("connection/port",           settings.port->text());
			s.setValue("connection/rpcuser",        settings.rpcuser->text());
			s.setValue("connection/rpcpassword",    settings.rpcpassword->text());

			s.sync();

			// Then refresh everything.
			this->rpc->reloadConnectionInfo();
			this->rpc->refresh();
		};
	});

    // Set up exit action
    QObject::connect(ui->actionExit, &QAction::triggered, this, &MainWindow::close);

    // Set up donate action
    QObject::connect(ui->actionDonate, &QAction::triggered, this, &MainWindow::donate);

    // Set up check for updates action
    QObject::connect(ui->actionCheck_for_Updates, &QAction::triggered, [=] () {
        QDesktopServices::openUrl(QUrl("https://github.com/adityapk00/zcash-qt-wallet/releases"));
    });

    QObject::connect(ui->actionImport_Private_Keys, &QAction::triggered, this, &MainWindow::importPrivKeys);

    // Set up about action
    QObject::connect(ui->actionAbout, &QAction::triggered, [=] () {
        QDialog aboutDialog(this);
        Ui_about about;
		about.setupUi(&aboutDialog);

		QString version	= QString("Version ") % QString(APP_VERSION) % " (" % QString(__DATE__) % ")";
		about.versionLabel->setText(version);
        
        aboutDialog.exec();
    });

    // Initialize to the balances tab
    ui->tabWidget->setCurrentIndex(0);

    setupSendTab();
    setupTransactionsTab();
    setupRecieveTab();
    setupBalancesTab();

    rpc = new RPC(new QNetworkAccessManager(this), this);
    rpc->refresh();
}

void MainWindow::donate() {
    // Set up a donation to me :)
    ui->Address1->setText("zcEgrceTwvoiFdEvPWcsJHAMrpLsprMF6aRJiQa3fan5ZphyXLPuHghnEPrEPRoEVzUy65GnMVyCTRdkT6BYBepnXh6NBYs");
    ui->Address1->setCursorPosition(0);
    ui->Amount1->setText("0.01");

    ui->statusBar->showMessage("Donate 0.01 " % Utils::getTokenName() % " to support zcash-qt-wallet");

    // And switch to the send tab.
    ui->tabWidget->setCurrentIndex(1);
}

void MainWindow::importPrivKeys() {
    bool ok;
    QString text = QInputDialog::getMultiLineText(
                        this, "Import Private Keys", 
                        QString() + 
                        "Please paste your private keys (zAddr or tAddr) here, one per line.\n" +
                        "The keys will be imported into your connected zcashd node", 
                        "", &ok);
    if (ok && !text.isEmpty()) {
        auto keys = text.split("\n");
        for (int i=0; i < keys.length(); i++) {
            auto key = keys[i].trimmed();
            if (key.startsWith("S") ||
                key.startsWith("secret")) { // Z key

            } else {    // T Key

            }
        }
    }
}

void MainWindow::setupBalancesTab() {
    ui->unconfirmedWarning->setVisible(false);

    // Setup context menu on balances tab
    ui->balancesTable->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(ui->balancesTable, &QTableView::customContextMenuRequested, [=] (QPoint pos) {
        QModelIndex index = ui->balancesTable->indexAt(pos);
        if (index.row() < 0) return;

        index = index.sibling(index.row(), 0);
        auto addr = ui->balancesTable->model()->data(index).toString();

        QMenu menu(this);

        menu.addAction("Copy Address", [=] () {
            QClipboard *clipboard = QGuiApplication::clipboard();
            clipboard->setText(addr);            
        });

        if (addr.startsWith("t")) {
            menu.addAction("View on block explorer", [=] () {
                QString url;
                if (Settings::getInstance()->isTestnet()) {
                    url = "https://explorer.testnet.z.cash/address/" + addr;
                } else {
                    url = "https://explorer.zcha.in/accounts/" + addr;
                }
                QDesktopServices::openUrl(QUrl(url));
            });
        }

        menu.exec(ui->balancesTable->viewport()->mapToGlobal(pos));            
    });
}

void MainWindow::setupTransactionsTab() {
    // Set up context menu on transactions tab
    ui->transactionsTable->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(ui->transactionsTable, &QTableView::customContextMenuRequested, [=] (QPoint pos) {
        QModelIndex index = ui->transactionsTable->indexAt(pos);
        if (index.row() < 0) return;

        QMenu menu(this);

        auto txModel = dynamic_cast<TxTableModel *>(ui->transactionsTable->model());
        QString txid = txModel->getTxId(index.row());

        menu.addAction("Copy txid to clipboard", [=] () {            
            QGuiApplication::clipboard()->setText(txid);
        });
        menu.addAction("View on block explorer", [=] () {
            QString url;
            if (Settings::getInstance()->isTestnet()) {
                url = "https://explorer.testnet.z.cash/tx/" + txid;
            } else {
                url = "https://explorer.zcha.in/transactions/" + txid;
            }
            QDesktopServices::openUrl(QUrl(url));
        });

        menu.exec(ui->transactionsTable->viewport()->mapToGlobal(pos));        
    });
}

void MainWindow::setupRecieveTab() {
    auto addNewTAddr = [=] () {
        rpc->newTaddr([=] (json reply) {
                QString addr = QString::fromStdString(reply.get<json::string_t>());

                // Just double make sure the T-address is still checked
                if (ui->rdioTAddr->isChecked()) {
                    ui->listRecieveAddresses->insertItem(0, addr);
                    ui->listRecieveAddresses->setCurrentIndex(0);

                    ui->statusBar->showMessage("Created new recieving tAddr", 10 * 1000);
                }
            });
    };

    // Connect t-addr radio button
    QObject::connect(ui->rdioTAddr, &QRadioButton::toggled, [=] (bool checked) { 
        // Whenever the T-address is selected, we generate a new address, because we don't
        // want to reuse T-addrs
        if (checked && this->rpc->getUTXOs() != nullptr) { 
            auto utxos = this->rpc->getUTXOs();
            ui->listRecieveAddresses->clear();

            std::for_each(utxos->begin(), utxos->end(), [=] (auto& utxo) {
                auto addr = utxo.address;
                if (addr.startsWith("t") && ui->listRecieveAddresses->findText(addr) < 0) {
                    ui->listRecieveAddresses->addItem(addr);
                }
            });

            addNewTAddr();
        } 
    });

    auto addNewZaddr = [=] () {
        rpc->newZaddr([=] (json reply) {
            QString addr = QString::fromStdString(reply.get<json::string_t>());
            // Make sure the RPC class reloads the Z-addrs for future use
            rpc->refreshAddresses();

            // Just double make sure the Z-address is still checked
            if (ui->rdioZAddr->isChecked()) {
                ui->listRecieveAddresses->insertItem(0, addr);
                ui->listRecieveAddresses->setCurrentIndex(0);

                ui->statusBar->showMessage("Created new zAddr", 10 * 1000);
            }
        });
    };

    auto addZAddrsToComboList = [=] (bool checked) { 
        if (checked && this->rpc->getAllZAddresses() != nullptr) { 
            auto addrs = this->rpc->getAllZAddresses();
            ui->listRecieveAddresses->clear();

            std::for_each(addrs->begin(), addrs->end(), [=] (auto addr) {
                    ui->listRecieveAddresses->addItem(addr);
            }); 

            // If z-addrs are empty, then create a new one.
            if (addrs->isEmpty()) {
                addNewZaddr();
            }
        } 
    };

    QObject::connect(ui->rdioZAddr, &QRadioButton::toggled, addZAddrsToComboList);

    // Explicitly get new address button.
    QObject::connect(ui->btnRecieveNewAddr, &QPushButton::clicked, [=] () {
        if (ui->rdioZAddr->isChecked()) {
            addNewZaddr(); 
        } else if (ui->rdioTAddr->isChecked()) {
            addNewTAddr();
        }
    });

    // Focus enter for the Recieve Tab
    QObject::connect(ui->tabWidget, &QTabWidget::currentChanged, [=] (int tab) {
        if (tab == 2) {
            // Switched to recieve tab, so update everything. 
            
            // Set the radio button to "Z-Addr", which should update the Address combo
            ui->rdioZAddr->setChecked(true);

            // And then select the first one
            ui->listRecieveAddresses->setCurrentIndex(0);
        }
    });

    QObject::connect(ui->listRecieveAddresses, 
        QOverload<const QString &>::of(&QComboBox::currentIndexChanged), [=] (const QString& addr) {
        if (addr.isEmpty()) {
            // Draw empty stuff

            ui->txtRecieve->clear();
            ui->qrcodeDisplay->clear();
            return;
        }

        ui->txtRecieve->setPlainText(addr);       
        
        QSize sz = ui->qrcodeDisplay->size();

        QPixmap pm(sz);
        pm.fill(Qt::white);
        QPainter painter(&pm);
        
        // NOTE: At this point you will use the API to get the encoding and format you want, instead of my hardcoded stuff:
        qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText(addr.toUtf8().constData(), qrcodegen::QrCode::Ecc::LOW);
        const int    s      = qr.getSize()>0?qr.getSize():1;
        const double w      = sz.width();
        const double h      = sz.height();
        const double aspect = w/h;
        const double size   = ((aspect>1.0)?h:w);
        const double scale  = size/(s+2);
		const double offset = (w - size) > 0 ? (w - size) / 2 : 0;
        // NOTE: For performance reasons my implementation only draws the foreground parts in supplied color.
        // It expects background to be prepared already (in white or whatever is preferred).
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(Qt::black));
        for(int y=0; y<s; y++) {
            for(int x=0; x<s; x++) {
                const int color=qr.getModule(x, y);  // 0 for white, 1 for black
                if(0!=color) {
                    const double rx1=(x+1)*scale+ offset, ry1=(y+1)*scale;
                    QRectF r(rx1, ry1, scale, scale);
                    painter.drawRects(&r,1);
                }
            }
        }
        
        ui->qrcodeDisplay->setPixmap(pm);
    });    

}

MainWindow::~MainWindow()
{
    delete ui;
    delete rpc;

    delete loadingMovie;
}