#include <QApplication>
#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QPushButton>
#include <QDragEnterEvent>
#include <iostream>
#include <QDropEvent>
#include <QMimeData>
#include <QSettings>
#include <QFileDialog>
#include <QComboBox>
#include <QMessageBox>
#include <QProgressBar>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QDir>
#include <QFile>
#include <QCheckBox>
#include <QFileInfo>
#include <QDesktopServices>
#include <QUrl>
#include "audio_converter.h"
#include "transcriber.h"
#include "subtitle_writer.h"
#include "whisper.h"
#include <QProcess>
#include <QTableWidget>
#include <QHeaderView>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QSplitter>
#include <QTimer>
#include <QThread>
#include <QGroupBox>
#include <QCoreApplication>
#include <QMap>
#include "ggml-backend.h"
#include <QStandardPaths>
#include <fstream>
#include <QStandardPaths>
#include <QDir>

struct ModelInfo {
    QString displayName;
    QString fileName;   // models/ klasöründeki dosya adı
    QString url;         // indirme linki
    QString ramInfo;
    QString diskInfo;
    QString sizeKey;
};

Q_DECLARE_METATYPE(std::vector<Segment>)
class TranscriptionWorker : public QObject {
    Q_OBJECT
public:
    QString filePath;
    QString modelPath;
    std::string language;
    bool useVad;
    std::string vadModelPath;
    float entropyThold, logprobThold, noSpeechThold, vadThreshold;
    QString outputBaseName;
    std::string modelSize;
    int gpuDeviceIndex = -1;

public slots:
    void process() {
        QString ffmpegOutPathQt;
        if (!ffmpegConvert(filePath, ffmpegOutPathQt)) {
            emit error("ffmpeg dönüşümü başarısız.");
            return;
        }
        std::string ffmpegOutPath = ffmpegOutPathQt.toStdString();

        std::vector<float> pcmf32;
        if (!read_wav(ffmpegOutPath, pcmf32)) {
            std::remove(ffmpegOutPath.c_str());
            emit error("WAV okunamadı.");
            return;
        }
        std::remove(ffmpegOutPath.c_str());

        whisper_context* ctx = nullptr;
        if (!transcribe(modelPath.toStdString(), pcmf32, ctx, language, useVad, vadModelPath,
                entropyThold, logprobThold, noSpeechThold, vadThreshold,
                modelSize, gpuDeviceIndex,
                &TranscriptionWorker::onProgress, this)) {
            emit error("Transkript başarısız.");
            return;
        }

        const float thold = 0.5f;
        std::vector<Segment> segments = extract_segments(ctx, thold);
        whisper_free(ctx);

        emit finished(segments, outputBaseName);
    }

signals:
    void finished(std::vector<Segment> segments, QString outputPath);
    void error(QString message);
    void progressUpdated(int percent);

private:
    

    static void onProgress(int progress, void* userData) {
        auto* self = static_cast<TranscriptionWorker*>(userData);
        emit self->progressUpdated(progress);
    }

    bool ffmpegConvert(const QString& audioPath, QString& outWavPath) {
        // Write temporary WAV to a guaranteed writable location
        outWavPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/gecici_ses.wav";
        QString ffmpegExe = "ffmpeg"; 

        QProcess checkProcess;
        checkProcess.start("ffmpeg", {"-version"});
        bool systemFfmpegAvailable = checkProcess.waitForStarted(1000) && checkProcess.waitForFinished(3000);

        if (!systemFfmpegAvailable) {
            ffmpegExe = QCoreApplication::applicationDirPath() + "/ffmpeg/ffmpeg.exe";
            std::cerr << "Sistem ffmpeg bulunamadi, gomulu kopya denenecek: " << ffmpegExe.toStdString() << std::endl;
            
            if (!QFile::exists(ffmpegExe)) {
                std::cerr << "KRITIK HATA: Gomulu ffmpeg.exe bu yolda bulunamadi!" << std::endl;
                return false;
            }
        } else {
            std::cerr << "Sistem PATH uzerinde ffmpeg bulundu." << std::endl;
        }

        QProcess process;
        process.setProgram(ffmpegExe);
        process.setArguments({"-i", audioPath, "-ar", "16000", "-ac", "1", "-c:a", "pcm_s16le", outWavPath, "-y"});
        
        std::cerr << "ffmpeg baslatiliyor... Komut: " << ffmpegExe.toStdString() << std::endl;
        process.start();

        if (!process.waitForFinished(-1)) {
            std::cerr << "ffmpeg islemi tamamlanamadi. QProcess hatasi: " 
                    << process.errorString().toStdString() << std::endl;
            return false;
        }

        if (process.exitCode() != 0) {
            std::cerr << "ffmpeg basarisiz oldu. Cikis kodu: " << process.exitCode() << std::endl;
            std::cerr << "ffmpeg stderr ciktisi: " << process.readAllStandardError().toStdString() << std::endl;
            return false;
        }

        std::cerr << "ffmpeg donusumu basarili. Dosya: " << outWavPath.toStdString() << std::endl;
        return true;
    }
};

class DropArea : public QWidget {
    Q_OBJECT
public:

    
    DropArea(QWidget* parent = nullptr) : QWidget(parent) {
        qRegisterMetaType<std::vector<Segment>>("std::vector<Segment>");
        setAcceptDrops(true);
        settings = new QSettings("AltyaziProjesi", "AltyaziAraci", this);
        network = new QNetworkAccessManager(this);

        appLanguageLabel = new QLabel(this);
        appLanguageCombo = new QComboBox(this);

        appLanguageCombo->addItem("Türkçe", "tr");
        appLanguageCombo->addItem("English", "en");
        QString savedAppLang = settings->value("appLanguage", "tr").toString();
        int appLangIndex = appLanguageCombo->findData(savedAppLang);
        if (appLangIndex != -1) appLanguageCombo->setCurrentIndex(appLangIndex);
        connect(appLanguageCombo, &QComboBox::currentIndexChanged, this, &DropArea::appLanguageChanged);

        auto* mainLayout = new QVBoxLayout(this);
        auto* splitter = new QSplitter(Qt::Horizontal, this);

        auto* leftPanel = new QWidget(this);
        auto* layout = new QVBoxLayout(leftPanel); // artık tüm ayar grupların BURAYA ekleniyor (değişmedi)

        auto* rightPanel = new QWidget(this);
        auto* rightLayout = new QVBoxLayout(rightPanel);

        /*rightLayout->addWidget(segmentTable);
        rightLayout->addWidget(saveButton);

        splitter->addWidget(leftPanel);
        splitter->addWidget(rightPanel);
        splitter->setStretchFactor(1, 1); // sağ panel (tablo) esnesin

        mainLayout->addWidget(splitter);*/

        statusLabel = new QLabel("Bir dosyayı buraya sürükle", this);
        statusLabel->setAlignment(Qt::AlignCenter);
        statusLabel->setStyleSheet("QLabel { border: 2px dashed #888; padding: 40px; }");

        QString savedFolder = settings->value("outputFolder", "").toString();
        outputFolderLabel = new QLabel(
            savedFolder.isEmpty() ? T("output_not_selected") : T("output_selected").arg(savedFolder),
            this
        );
        
        chooseFolderButton = new QPushButton("Çıktı Klasörü Seç", this);
        connect(chooseFolderButton, &QPushButton::clicked, this, &DropArea::chooseOutputFolder);

        languageCombo = new QComboBox(this);
        languageCombo->addItem("Türkçe", "tr");
        languageCombo->addItem("İngilizce", "en");
        languageCombo->addItem("Otomatik Algıla", "auto");
        QString savedLang = settings->value("language", "tr").toString();
        int langIndex = languageCombo->findData(savedLang);
        if (langIndex != -1) languageCombo->setCurrentIndex(langIndex);
        connect(languageCombo, &QComboBox::currentIndexChanged, this, &DropArea::languageChanged);

        setupModels();
        modelCombo = new QComboBox(this);
        for (const auto& m : models) {
            modelCombo->addItem(m.displayName);
        }
        downloadButton = new QPushButton("Modeli İndir", this);
        connect(downloadButton, &QPushButton::clicked, this, &DropArea::downloadButtonClicked);
        //layout->addWidget(downloadButton);

        QString savedModel = settings->value("model", models.first().displayName).toString();
        int modelIndex = modelCombo->findText(savedModel);
        if (modelIndex != -1) modelCombo->setCurrentIndex(modelIndex);
        connect(modelCombo, &QComboBox::currentIndexChanged, this, &DropArea::modelChanged);

        vadCheckbox = new QCheckBox("VAD (Ses Aktivite Tespiti) Aktif", this);

        vadAggressivenessLabel = new QLabel("VAD Agresiflik Seviyesi:", this);

        vadAggressivenessCombo = new QComboBox(this);
        vadAggressivenessCombo->addItem("Toleranslı (0.5)", 0.5);
        vadAggressivenessCombo->addItem("Dengeli (0.7)", 0.7);
        vadAggressivenessCombo->addItem("Agresif (0.9)", 0.9);

        double savedVadThreshold = settings->value("vadThreshold", 0.7).toDouble();
        int vadIndex = vadAggressivenessCombo->findData(savedVadThreshold);
        if (vadIndex != -1) vadAggressivenessCombo->setCurrentIndex(vadIndex);
        connect(vadAggressivenessCombo, &QComboBox::currentIndexChanged, this, &DropArea::vadAggressivenessChanged);

        //layout->addWidget(vadAggressivenessCombo);

        qualityLabel = new QLabel("Transkript Hassasiyeti:", this);

        qualityCombo = new QComboBox(this);
        qualityCombo->addItem("Hızlı (daha az yeniden-deneme)", "fast");
        qualityCombo->addItem("Dengeli (varsayılan)", "balanced");
        qualityCombo->addItem("Hassas (daha sık yeniden-deneme)", "precise");

        QString savedQuality = settings->value("quality", "balanced").toString();
        int qualityIndex = qualityCombo->findData(savedQuality);
        if (qualityIndex != -1) qualityCombo->setCurrentIndex(qualityIndex);
        connect(qualityCombo, &QComboBox::currentIndexChanged, this, &DropArea::qualityChanged);

        //layout->addWidget(qualityCombo);
        vadCheckbox->setChecked(settings->value("vadEnabled", true).toBool()); // varsayılan: açık (mevcut kararlı ayarınla aynı)
        connect(vadCheckbox, &QCheckBox::toggled, this, &DropArea::vadToggled);

        //layout->addWidget(vadCheckbox);

        downloadProgress = new QProgressBar(this);
        downloadProgress->setVisible(false);

        transcribeProgress = new QProgressBar(this);
        transcribeProgress->setRange(0, 100);
        transcribeProgress->setVisible(false);
        //layout->addWidget(transcribeProgress);

        startButton = new QPushButton("Başlat", this);
        startButton->setEnabled(false);
        //yazi kismi
        segmentTable = new QTableWidget(this);
        segmentTable->setColumnCount(2);
        segmentTable->setHorizontalHeaderLabels({"Zaman", "Metin"});
        segmentTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        segmentTable->setVisible(false); // transkript bitene kadar gizli

        saveButton = new QPushButton("Altyazıyı Kaydet", this);
        saveButton->setVisible(false);
        connect(saveButton, &QPushButton::clicked, this, &DropArea::saveEditedSubtitles);

        discardButton = new QPushButton("Vazgeç", this);
        discardButton->setVisible(false);
        connect(discardButton, &QPushButton::clicked, this, &DropArea::discardTranscription);

        
        rightLayout->addWidget(segmentTable);
        rightLayout->addWidget(saveButton);
        rightLayout->addWidget(discardButton);

        openFolderButton = new QPushButton("Çıktı Klasörünü Aç", this);
        openFolderButton->setEnabled(!settings->value("outputFolder", "").toString().isEmpty());
        connect(openFolderButton, &QPushButton::clicked, this, &DropArea::openOutputFolder);

        //layout->addWidget(openFolderButton);
        connect(startButton, &QPushButton::clicked, this, &DropArea::startTranscription);

        QString savedModelsFolder = settings->value("modelsFolder", "").toString();
        if (savedModelsFolder.isEmpty()) {
            savedModelsFolder = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/models";
            QDir().mkpath(savedModelsFolder);
            settings->setValue("modelsFolder", savedModelsFolder);
        }
        modelsFolderLabel = new QLabel(T("models_folder").arg(savedModelsFolder), this);
        chooseModelsFolderButton = new QPushButton("Model Klasörü Seç", this);
        connect(chooseModelsFolderButton, &QPushButton::clicked, this, &DropArea::chooseModelsFolder);

        //layout->addWidget(modelsFolderLabel);
        //layout->addWidget(chooseModelsFolderButton);

        openModelsFolderButton = new QPushButton(T("open_models_folder"), this);
        connect(openModelsFolderButton, &QPushButton::clicked, this, &DropArea::openModelsFolder);

        //layout->addWidget(openModelsFolderButton);

        glowTimer = new QTimer(this);
        connect(glowTimer, &QTimer::timeout, this, &DropArea::toggleGlow);
        glowTimer->start(500); // her 500ms'de bir yanıp söner

        //layout->addWidget(statusLabel);
        //layout->addWidget(outputFolderLabel);
        //layout->addWidget(chooseFolderButton);
        //layout->addWidget(languageCombo);
        //layout->addWidget(modelCombo);
        //layout->addWidget(downloadProgress);
        //layout->addWidget(startButton);

        // --- Çıktı grubu (SEÇ önce, AÇ sonra) ---
        outputGroup = new QGroupBox("Çıktı Ayarları", this);
        auto* outputLayout = new QVBoxLayout(outputGroup);
        outputLayout->addWidget(outputFolderLabel);
        outputLayout->addWidget(chooseFolderButton);
        outputLayout->addWidget(openFolderButton);

        // --- Model grubu ---
        modelGroup = new QGroupBox("Model Ayarları", this);
        auto* modelLayout = new QVBoxLayout(modelGroup);
        modelLayout->addWidget(modelsFolderLabel);
        modelLayout->addWidget(chooseModelsFolderButton);
        modelLayout->addWidget(openModelsFolderButton);
        modelLayout->addWidget(modelCombo);
        modelLayout->addWidget(downloadButton);
        modelLayout->addWidget(downloadProgress);

        

        // --- VAD grubu ---
        vadGroup = new QGroupBox("Ses Aktivite Tespiti (VAD)", this);
        auto* vadLayout = new QVBoxLayout(vadGroup);
        vadLayout->addWidget(vadCheckbox);
        vadLayout->addWidget(vadAggressivenessLabel);
        vadLayout->addWidget(vadAggressivenessCombo);

        // --- Transkript kalite/dil grubu ---
        qualityGroup = new QGroupBox("Transkript Ayarları", this);
        auto* qualityLayout = new QVBoxLayout(qualityGroup);
        qualityLayout->addWidget(languageCombo);
        qualityLayout->addWidget(qualityLabel);
        qualityLayout->addWidget(qualityCombo);

        auto devices = list_available_devices();
        auto* deviceLabel = new QLabel(this);
        deviceCombo = new QComboBox(this);
        deviceCombo->addItem("CPU", -1); // her zaman ilk seçenek

        for (const auto& dev : devices) {
            if (dev.isGpu) {
                deviceCombo->addItem(QString::fromStdString(dev.name), dev.index);
            }
        }

        int savedDeviceIndex = settings->value("gpuDeviceIndex", -1).toInt();
        int comboIndex = deviceCombo->findData(savedDeviceIndex);
        if (comboIndex != -1) deviceCombo->setCurrentIndex(comboIndex);
        connect(deviceCombo, &QComboBox::currentIndexChanged, this, &DropArea::deviceChanged);

        deviceGroup = new QGroupBox(T("device_group"), this);
        auto* deviceLayout = new QVBoxLayout(deviceGroup);
        deviceLayout->addWidget(deviceCombo);


        layout->addWidget(appLanguageLabel);
        layout->addWidget(appLanguageCombo);
        // --- Sol panele grupları ve kalan widget'ları ekle ---
        layout->addWidget(statusLabel);
        layout->addWidget(outputGroup);
        layout->addWidget(modelGroup);
        layout->addWidget(vadGroup);
        layout->addWidget(qualityGroup);
        layout->addWidget(deviceGroup);
        layout->addWidget(transcribeProgress);
        layout->addWidget(startButton);

        splitter->addWidget(leftPanel);
        splitter->addWidget(rightPanel);
        splitter->setStretchFactor(0, 0); // sol panel sabit boyutlu kalsın
        splitter->setStretchFactor(1, 1); // sağ panel (tablo) esnesin

        mainLayout->addWidget(splitter);

        // Açılışta seçili modelin durumunu kontrol et (indirilmiş mi diye)
        updateModelStatusUI(modelCombo->currentIndex());
        retranslateUI();
    }

protected:
    void dragEnterEvent(QDragEnterEvent* event) override {
        if (event->mimeData()->hasUrls()) event->acceptProposedAction();
    }

    void dropEvent(QDropEvent* event) override {
        const QList<QUrl> urls = event->mimeData()->urls();
        if (urls.isEmpty()) return;
        QString filePath = urls.first().toLocalFile();
        if (segmentTable->isVisible()) {
            // Kaydedilmemiş bir transkript varken yeni dosya sürüklendi — reddet
            QMessageBox::information(this, T("pending_title"), T("pending_message"));
            return;
        }
        statusLabel->setText(T("selected_file").arg(filePath));
        selectedFilePath = filePath;
        updateModelStatusUI(modelCombo->currentIndex());
        refreshStartButtonState();
        startGlowing(startButton);
    }

private slots:
    void onTranscriptionFinished(std::vector<Segment> segments, QString outputPath) {
        transcribeProgress->setVisible(false);
        currentSegments = segments;
        pendingOutputPath = outputPath;
        populateSegmentTable();

        statusLabel->setText(T("finished_edit"));
        segmentTable->setVisible(true);
        saveButton->setVisible(true);
        discardButton->setVisible(true);
        startGlowing(saveButton);

        openFolderButton->setEnabled(true);
        // startButton->setEnabled(true);   // <-- BU SATIRI SİL, artık kaydedilene kadar kapalı kalsın
        updateModelStatusUI(modelCombo->currentIndex());
    }

    void onTranscriptionError(QString message) {
        transcribeProgress->setVisible(false);
        QMessageBox::critical(this, T("error_title"), T("error_transcribe"));
        startButton->setEnabled(true);
        languageCombo->setEnabled(true);
        modelCombo->setEnabled(true);
        qualityCombo->setEnabled(true);
        vadCheckbox->setEnabled(true);
        vadAggressivenessCombo->setEnabled(true);
        deviceCombo->setEnabled(true);
        updateModelStatusUI(modelCombo->currentIndex());
    }
    void chooseOutputFolder() {
        QString folder = QFileDialog::getExistingDirectory(this, T("choose_output_folder"));
        if (folder.isEmpty()) return;
        settings->setValue("outputFolder", folder);
        outputFolderLabel->setText(T("output_selected").arg(folder));
        openFolderButton->setEnabled(true);
    }
    void toggleGlow() {
        if (!glowingButton) return;
        glowOn = !glowOn;
        if (glowOn) {
            glowingButton->setStyleSheet("QPushButton { background-color: #4CAF50; font-weight: bold; }");
        } else {
            glowingButton->setStyleSheet("");
        }
    }

    void startGlowing(QPushButton* button) {
        glowingButton = button;
    }

    void stopGlowing(QPushButton* button) {
        if (glowingButton == button) {
            glowingButton = nullptr;
            button->setStyleSheet("");
        }
    }

    void languageChanged() {
        settings->setValue("language", languageCombo->currentData().toString());
    }

    void modelChanged(int index) {
        settings->setValue("model", modelCombo->currentText());

        const ModelInfo& m = models[index];
        QMessageBox::information(this, T("model_info_title"),
                     T("model_info_body").arg(m.displayName, m.ramInfo, m.diskInfo));

        updateModelStatusUI(index);
        refreshStartButtonState();
    }
    void downloadButtonClicked() {
        int index = modelCombo->currentIndex();
        const ModelInfo& m = models[index];
        QString fullPath = modelsFolderPath() + "/" + m.fileName;
        downloadModel(m, fullPath);
    }

    void vadToggled(bool checked) {
        settings->setValue("vadEnabled", checked);

    
    }
    void qualityChanged() {
        settings->setValue("quality", qualityCombo->currentData().toString());
    }
    void vadAggressivenessChanged() {
        settings->setValue("vadThreshold", vadAggressivenessCombo->currentData().toDouble());
    }
    void chooseModelsFolder() {
        QString folder = QFileDialog::getExistingDirectory(this, T("choose_models_folder"));
        if (folder.isEmpty()) return;
        settings->setValue("modelsFolder", folder);
        modelsFolderLabel->setText(T("models_folder").arg(folder));
        updateModelStatusUI(modelCombo->currentIndex()); // yeni klasördeki durumu yansıt
        refreshStartButtonState();
    }

    
    void startTranscription() {
        QString outputFolder = settings->value("outputFolder", "").toString();
        if (outputFolder.isEmpty() || selectedFilePath.isEmpty()) {
            QMessageBox::warning(this, T("missing_info_title"), T("missing_info_message"));
            return;
        }

        stopGlowing(startButton);
        startButton->setEnabled(false);
        downloadButton->setEnabled(false);
        languageCombo->setEnabled(false);
        modelCombo->setEnabled(false);
        qualityCombo->setEnabled(false);
        vadCheckbox->setEnabled(false);
        vadAggressivenessCombo->setEnabled(false);
        deviceCombo->setEnabled(false);
        transcribeProgress->setVisible(true);
        transcribeProgress->setValue(0);
        statusLabel->setText(T("processing"));

        int modelIndex = modelCombo->currentIndex();
        QString modelPath = modelsFolderPath() + "/" + models[modelIndex].fileName;
        QualityPreset preset = getQualityPreset(qualityCombo->currentData().toString());

        auto* thread = new QThread(this);
        auto* worker = new TranscriptionWorker();

        worker->filePath = selectedFilePath;
        worker->modelPath = modelPath;
        worker->language = languageCombo->currentData().toString().toStdString();
        worker->useVad = vadCheckbox->isChecked();
        worker->vadModelPath = (QCoreApplication::applicationDirPath() + "/models/ggml-silero-v6.2.0.bin").toStdString();
        worker->entropyThold = preset.entropy_thold;
        worker->logprobThold = preset.logprob_thold;
        worker->noSpeechThold = preset.no_speech_thold;
        worker->vadThreshold = static_cast<float>(vadAggressivenessCombo->currentData().toDouble());
        worker->modelSize = models[modelIndex].sizeKey.toStdString();
        worker->gpuDeviceIndex = deviceCombo->currentData().toInt();

        QFileInfo fileInfo(selectedFilePath);
        worker->outputBaseName = outputFolder + "/" + fileInfo.completeBaseName();

        worker->moveToThread(thread);

        connect(thread, &QThread::started, worker, &TranscriptionWorker::process);
        connect(worker, &TranscriptionWorker::finished, this, &DropArea::onTranscriptionFinished);
        connect(worker, &TranscriptionWorker::progressUpdated, this, &DropArea::onProgressUpdated);
        connect(worker, &TranscriptionWorker::error, this, &DropArea::onTranscriptionError);
        connect(worker, &TranscriptionWorker::finished, thread, &QThread::quit);
        connect(worker, &TranscriptionWorker::error, thread, &QThread::quit);
        connect(thread, &QThread::finished, worker, &QObject::deleteLater);
        connect(thread, &QThread::finished, thread, &QObject::deleteLater);

        thread->start();
    }
    void saveEditedSubtitles() {
        stopGlowing(saveButton);
        for (int i = 0; i < segmentTable->rowCount(); i++) {
            QTableWidgetItem* item = segmentTable->item(i, 1);
            if (item) {
                std::string newText = item->text().toStdString();
                currentSegments[i].srtText = newText;

                if (newText != currentSegments[i].originalSrtText) {
                    currentSegments[i].assText = rebuild_karaoke(currentSegments[i].assText, newText);
                }
            }
        }

        if (!write_subtitles(currentSegments, pendingOutputPath.toStdString())) {
            QMessageBox::critical(this, "Hata", "Altyazı yazılamadı.");
            return;
        }

        
        segmentTable->setVisible(false);
        saveButton->setVisible(false);
        discardButton->setVisible(false);
        startButton->setEnabled(true);
        languageCombo->setEnabled(true);
        modelCombo->setEnabled(true);
        qualityCombo->setEnabled(true);
        vadCheckbox->setEnabled(true);
        vadAggressivenessCombo->setEnabled(true);
        deviceCombo->setEnabled(true);
        selectedFilePath.clear();
        statusLabel->setText(T("saved").arg(pendingOutputPath));
        //statusLabel->setText("Kaydedildi!\n" + pendingOutputPath + ".srt / .ass\n\nYeni bir dosya sürükleyebilirsin.");
    }

    void discardTranscription() {
        auto reply = QMessageBox::question(this, T("discard_title"), T("discard_confirm"), 
                     QMessageBox::Yes | QMessageBox::No);

        if (reply != QMessageBox::Yes) return;

        currentSegments.clear();
        segmentTable->setRowCount(0);
        segmentTable->setVisible(false);
        saveButton->setVisible(false);
        discardButton->setVisible(false);
        stopGlowing(saveButton);

        languageCombo->setEnabled(true);
        modelCombo->setEnabled(true);
        qualityCombo->setEnabled(true);
        vadCheckbox->setEnabled(true);
        vadAggressivenessCombo->setEnabled(true);
        deviceCombo->setEnabled(true);

        selectedFilePath.clear();
        statusLabel->setText(T("discarded"));
        startButton->setEnabled(false);
    }

    void openOutputFolder() {
            QString folder = settings->value("outputFolder", "").toString();
            if (!folder.isEmpty()) {
                QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
            }
        }
    void openModelsFolder() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(modelsFolderPath()));
    }
    void onProgressUpdated(int percent) {
        transcribeProgress->setValue(percent);
    }
private:
    QString T(const QString& key) {
        static const QMap<QString, QPair<QString, QString>> dict = {
            {"drop_hint", {"Bir dosyayı buraya sürükle", "Drag a file here"}},
            {"output_group", {"Çıktı Ayarları", "Output Settings"}},
            {"output_not_selected", {"Çıktı klasörü seçilmedi", "No output folder selected"}},
            {"output_selected", {"Çıktı klasörü: %1", "Output folder: %1"}},
            {"choose_output_folder", {"Çıktı Klasörü Seç", "Choose Output Folder"}},
            {"open_output_folder", {"Çıktı Klasörünü Aç", "Open Output Folder"}},
            {"model_group", {"Model Ayarları", "Model Settings"}},
            {"models_folder", {"Model klasörü: %1", "Models folder: %1"}},
            {"choose_models_folder", {"Model Klasörü Seç", "Choose Models Folder"}},
            {"open_models_folder", {"Model Klasörünü Aç", "Open Models Folder"}},
            {"download_model", {"Modeli İndir", "Download Model"}},
            {"model_downloaded", {"İndirildi ✓", "Downloaded ✓"}},
            {"vad_group", {"Ses Aktivite Tespiti (VAD)", "Voice Activity Detection (VAD)"}},
            {"vad_checkbox", {"VAD (Ses Aktivite Tespiti) Aktif", "VAD (Voice Activity Detection) Enabled"}},
            {"vad_aggressiveness", {"VAD Agresiflik Seviyesi:", "VAD Aggressiveness Level:"}},
            {"vad_tolerant", {"Toleranslı (0.5)", "Tolerant (0.5)"}},
            {"vad_balanced", {"Dengeli (0.7)", "Balanced (0.7)"}},
            {"vad_aggressive", {"Agresif (0.9)", "Aggressive (0.9)"}},
            {"quality_group", {"Transkript Ayarları", "Transcription Settings"}},
            {"quality_label", {"Transkript Hassasiyeti:", "Transcription Precision:"}},
            {"quality_fast", {"Hızlı (daha az yeniden-deneme)", "Fast (fewer retries)"}},
            {"quality_balanced", {"Dengeli (varsayılan)", "Balanced (default)"}},
            {"quality_precise", {"Hassas (daha sık yeniden-deneme)", "Precise (more retries)"}},
            {"lang_turkish", {"Türkçe", "Turkish"}},
            {"lang_english", {"İngilizce", "English"}},
            {"lang_auto", {"Otomatik Algıla", "Auto Detect"}},
            {"start_button", {"Başlat", "Start"}},
            {"save_button", {"Altyazıyı Kaydet", "Save Subtitles"}},
            {"discard_button", {"Vazgeç", "Discard"}},
            {"table_time", {"Zaman", "Time"}},
            {"table_text", {"Metin", "Text"}},
            {"selected_file", {"Seçilen dosya:\n%1", "Selected file:\n%1"}},
            {"processing", {"İşleniyor...", "Processing..."}},
            {"finished_edit", {"Transkript tamamlandı. Düzenleyip kaydedebilirsin.", "Transcription complete. You can edit and save."}},
            {"saved", {"Kaydedildi!\n%1.srt / .ass\n\nYeni bir dosya sürükleyebilirsin.", "Saved!\n%1.srt / .ass\n\nYou can drag a new file."}},
            {"discarded", {"Vazgeçildi. Yeni bir dosya sürükleyebilirsin.", "Discarded. You can drag a new file."}},
            {"pending_title", {"Bekleyen Altyazı", "Pending Subtitle"}},
            {"pending_message", {"Önce mevcut altyazıyı kaydet ya da vazgeç.", "Save or discard the current subtitle first."}},
            {"discard_title", {"Vazgeç", "Discard"}},
            {"discard_confirm", {"Bu altyazıyı kaydetmeden vazgeçmek istediğine emin misin? Düzenlemelerin kaybolacak.", "Are you sure you want to discard without saving? Your edits will be lost."}},
            {"missing_info_title", {"Eksik Bilgi", "Missing Information"}},
            {"missing_info_message", {"Dosya ve çıktı klasörü seçilmeli.", "A file and output folder must be selected."}},
            {"error_title", {"Hata", "Error"}},
            {"error_ffmpeg", {"ffmpeg dönüşümü başarısız.", "ffmpeg conversion failed."}},
            {"error_wav", {"WAV okunamadı.", "Could not read WAV."}},
            {"error_transcribe", {"Transkript başarısız.", "Transcription failed."}},
            {"error_write", {"Altyazı yazılamadı.", "Could not write subtitles."}},
            {"model_info_title", {"Model Bilgisi", "Model Info"}},
            {"model_info_body", {"Model: %1\nTahmini RAM: %2\nDisk boyutu: %3", "Model: %1\nEstimated RAM: %2\nDisk size: %3"}},
            {"download_error_title", {"İndirme Hatası", "Download Error"}},
            {"app_language_label", {"Arayüz Dili:", "Interface Language:"}},
            {"device_group", {"İşlemci", "Processing Device"}},
        };

        auto it = dict.find(key);
        if (it == dict.end()) return key;

        bool english = settings->value("appLanguage", "tr").toString() == "en";
        return english ? it.value().second : it.value().first;
    }

    void setupModels() {
        models = {
            {"Tiny", "ggml-tiny.bin",
            "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-tiny.bin",
            "~1 GB", "~75 MB", "tiny"},
            {"Base", "ggml-base.bin",
            "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.bin",
            "~1 GB", "~140 MB", "base"},
            {"Small", "ggml-small.bin",
            "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.bin",
            "~2 GB", "~460 MB", "small"},
            {"Medium", "ggml-medium.bin",
            "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-medium.bin",
            "~5 GB", "~1.5 GB", "medium"},
            {"Large v3", "ggml-large-v3.bin",
            "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-large-v3.bin",
            "~10 GB", "~3.1 GB", "large-v3"},
            {"Large v3 Turbo", "ggml-large-v3-turbo.bin",
            "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-large-v3-turbo.bin",
            "~6 GB", "~1.5 GB", "large-v3-turbo"}
        };
    }
    
    void retranslateUI() {
        setWindowTitle("Sifonsub");
        statusLabel->setText(T("drop_hint"));
        appLanguageLabel->setText(T("app_language_label"));

        deviceGroup->setTitle(T("device_group"));

        outputGroup->setTitle(T("output_group"));
        outputFolderLabel->setText(
            settings->value("outputFolder", "").toString().isEmpty()
                ? T("output_not_selected")
                : T("output_selected").arg(settings->value("outputFolder", "").toString())
        );
        chooseFolderButton->setText(T("choose_output_folder"));
        openFolderButton->setText(T("open_output_folder"));

        modelGroup->setTitle(T("model_group"));
        modelsFolderLabel->setText(T("models_folder").arg(modelsFolderPath()));
        chooseModelsFolderButton->setText(T("choose_models_folder"));
        openModelsFolderButton->setText(T("open_models_folder"));
        downloadButton->setText(downloadButton->isEnabled() ? T("download_model") : T("model_downloaded"));

        vadGroup->setTitle(T("vad_group"));
        vadCheckbox->setText(T("vad_checkbox"));
        vadAggressivenessLabel->setText(T("vad_aggressiveness"));
        vadAggressivenessCombo->setItemText(0, T("vad_tolerant"));
        vadAggressivenessCombo->setItemText(1, T("vad_balanced"));
        vadAggressivenessCombo->setItemText(2, T("vad_aggressive"));

        qualityGroup->setTitle(T("quality_group"));
        qualityLabel->setText(T("quality_label"));
        qualityCombo->setItemText(0, T("quality_fast"));
        qualityCombo->setItemText(1, T("quality_balanced"));
        qualityCombo->setItemText(2, T("quality_precise"));

        languageCombo->setItemText(0, T("lang_turkish"));
        languageCombo->setItemText(1, T("lang_english"));
        languageCombo->setItemText(2, T("lang_auto"));

        startButton->setText(T("start_button"));
        saveButton->setText(T("save_button"));
        discardButton->setText(T("discard_button"));
        segmentTable->setHorizontalHeaderLabels({T("table_time"), T("table_text")});
    }
    void populateSegmentTable() {
        segmentTable->setRowCount(static_cast<int>(currentSegments.size()));

        for (size_t i = 0; i < currentSegments.size(); i++) {
            const Segment& seg = currentSegments[i];

            QString timeLabel = QString::fromStdString(msToSrtTime(seg.t0 * 10))
                            + " → " + QString::fromStdString(msToSrtTime(seg.t1 * 10));

            auto* timeItem = new QTableWidgetItem(timeLabel);
            timeItem->setFlags(timeItem->flags() & ~Qt::ItemIsEditable); // zaman düzenlenemez

            auto* textItem = new QTableWidgetItem(QString::fromStdString(seg.srtText));
            // metin varsayılan olarak düzenlenebilir kalıyor

            segmentTable->setItem(static_cast<int>(i), 0, timeItem);
            segmentTable->setItem(static_cast<int>(i), 1, textItem);
        }
    }
    struct QualityPreset {
        float entropy_thold;
        float logprob_thold;
        float no_speech_thold;
    };

    bool ffmpegConvertQt(const QString& audioPath, QString& outWavPath) {
        outWavPath = "gecici_ses.wav";

        QProcess process;
        process.setProgram("ffmpeg");
        process.setArguments({
            "-i", audioPath,
            "-ar", "16000",
            "-ac", "1",
            "-c:a", "pcm_s16le",
            outWavPath,
            "-y"
        });

        process.start();
        if (!process.waitForFinished(-1)) {  // -1: süresiz bekle, ama UI thread'i bloke eder (thread konusunda çözülecek)
            return false;
        }

        return process.exitCode() == 0;
    }

    QualityPreset getQualityPreset(const QString& mode) {
        if (mode == "fast") {
            return {4.0f, -0.5f, 0.6f};      // daha az yeniden-deneme, daha toleranslı
        } else if (mode == "precise") {
            return {2.4f, -1.0f, 0.8f};      // daha sık yeniden-deneme, daha sıkı filtre
        }
        return {3.131f, -0.3f, 0.7f};        // "balanced" = senin mevcut kararlı ayarların
    }

    QString modelsFolderPath() {
        QString defaultPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/models";
        return settings->value("modelsFolder", defaultPath).toString();
    }

    void updateModelStatusUI(int index) {
        // Dropdown'daki TÜM modellerin yanına indirilmiş/indirilmemiş işareti koy
        for (int i = 0; i < models.size(); i++) {
            QString fullPath = modelsFolderPath() + "/" + models[i].fileName;
            QString label = models[i].displayName;
            if (QFile::exists(fullPath)) {
                label += " ✓";
            }
            modelCombo->setItemText(i, label);
        }

        if (isDownloading) return;

        const ModelInfo& m = models[index];
        QString fullPath = modelsFolderPath() + "/" + m.fileName;

        if (QFile::exists(fullPath)) {
            downloadButton->setEnabled(false);
            downloadButton->setText(T("model_downloaded"));
        } else {
            downloadButton->setEnabled(true);
            downloadButton->setText(T("download_model"));
            startButton->setEnabled(false);
        }
    }

    void refreshStartButtonState() {
        if (segmentTable->isVisible()) return; // düzenleme ekranı açıkken dokunma

        const ModelInfo& m = models[modelCombo->currentIndex()];
        bool modelExists = QFile::exists(modelsFolderPath() + "/" + m.fileName);

        startButton->setEnabled(modelExists && !selectedFilePath.isEmpty());
    }
    void appLanguageChanged() {
        settings->setValue("appLanguage", appLanguageCombo->currentData().toString());
        retranslateUI();
        updateModelStatusUI(modelCombo->currentIndex()); // ✓ işaretli model isimlerini de güncellemek için
    }
    void deviceChanged() {
        settings->setValue("gpuDeviceIndex", deviceCombo->currentData().toInt());
    }

    void downloadModel(const ModelInfo& m, const QString& savePath) {
        isDownloading = true;
        downloadButton->setEnabled(false);
        modelCombo->setEnabled(false);
        startButton->setEnabled(false);
        downloadProgress->setVisible(true);
        downloadProgress->setValue(0);

        QNetworkRequest request{QUrl(m.url)};
        QNetworkReply* reply = network->get(request);

        connect(reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) {
                if (total > 0) downloadProgress->setValue(int(100 * received / total));
            });

        connect(reply, &QNetworkReply::finished, this,
            [this, reply, savePath]() {
                if (reply->error() == QNetworkReply::NoError) {
                    QFile file(savePath);
                    if (file.open(QIODevice::WriteOnly)) {
                        file.write(reply->readAll());
                        file.close();
                    } else {
                        QMessageBox::critical(this, T("error_title"),
                            "Dosya yazılamadı: " + savePath + "\n" + file.errorString());
                    }
                } else {
                    QMessageBox::warning(this, T("download_error_title"), reply->errorString());
                }
                downloadProgress->setVisible(false);
                reply->deleteLater();

                isDownloading = false;
                modelCombo->setEnabled(true);
                updateModelStatusUI(modelCombo->currentIndex());
                refreshStartButtonState();
            });
    }

    QLabel* statusLabel;
    QLabel* outputFolderLabel;
    QPushButton* startButton;
    QSettings* settings;
    QComboBox* languageCombo;
    QComboBox* modelCombo;
    QProgressBar* downloadProgress;
    QNetworkAccessManager* network;
    QVector<ModelInfo> models;
    QCheckBox* vadCheckbox;
    QString selectedFilePath;
    QPushButton* openFolderButton;
    QComboBox* qualityCombo;
    QPushButton* downloadButton;
    QComboBox* vadAggressivenessCombo;
    QLabel* modelsFolderLabel;
    QPushButton* openModelsFolderButton;
    QTableWidget* segmentTable;
    QPushButton* saveButton;
    std::vector<Segment> currentSegments; // düzenlenmekte olan segmentler
    QString pendingOutputPath;             // "Kaydet"e basınca nereye yazılacak
    QTimer* glowTimer;
    QPushButton* glowingButton = nullptr;
    QProgressBar* transcribeProgress;
    QPushButton* discardButton;
    QComboBox* appLanguageCombo;
    QLabel* appLanguageLabel; // retranslate için saklıyoruz
    QGroupBox* outputGroup;
    QPushButton* chooseFolderButton;
    QGroupBox* modelGroup;
    QPushButton* chooseModelsFolderButton;
    QGroupBox* vadGroup;
    QLabel* vadAggressivenessLabel;
    QGroupBox* qualityGroup;
    QLabel* qualityLabel;
    std::string modelSize;
    QComboBox* deviceCombo;
    QGroupBox* deviceGroup;
    int gpuDeviceIndex = -1;   // <-- ekle, varsayılan CPU
    bool glowOn = false;
    bool isDownloading = false;
};


int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setOrganizationName("AltyaziProjesi");
    app.setApplicationName("AltyaziAraci");

    QString logDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(logDir);
    QString logPath = logDir + "/sifonsub_log.txt";

    freopen(logPath.toLocal8Bit().constData(), "w", stderr);
    freopen(logPath.toLocal8Bit().constData(), "a", stdout);

    DropArea window;
    window.setWindowTitle("Altyazi Araci - Qt Test");
    window.resize(900, 500);
    window.show();
    return app.exec();
}

#include "qt_test_main.moc"