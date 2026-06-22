

#include "window.h"
#include <QApplication>
#include <QMainWindow>
#include <QTextEdit>
#include <QLineEdit>
#include <QLabel>
#include <QHBoxLayout>
#include <QWidget>
#include <QMenu>
#include <QContextMenuEvent>
#include <QSysInfo>
#include <QScrollBar>
#include <QTextCursor>
#include <QThread>
#include <string>

#include "colors.h"
#include "history.h"
#include "parser.h"
#include "shell.h"


const char *BG_COLOR = "#121212"; 
const char *TEXT_COLOR = "#E0E0E0"; 
const char *PROMPT_COLOR = "#00A0A0"; 
const char *INFO_COLOR = "#808080"; 


class OutputView : public QTextEdit {
    Q_OBJECT

public:
    OutputView(QWidget *parent = nullptr) : QTextEdit(parent) {
    }

protected:
    void contextMenuEvent(QContextMenuEvent *event) override {
        QMenu *menu = createStandardContextMenu();
        
        for (QAction *action: menu->actions()) {
            if (!action->text().contains("Copy", Qt::CaseInsensitive) &&
                !action->text().contains("Select All", Qt::CaseInsensitive)) {
                menu->removeAction(action);
            }
        }
        menu->exec(event->globalPos());
        delete menu;
    }

    void mousePressEvent(QMouseEvent *event) override {
        QTextEdit::mousePressEvent(event); 
        emit clicked(); 
    }

Q_SIGNALS:
    void clicked();
};


class ParryShellWindow : public QMainWindow {
    Q_OBJECT

public:
    int history_index = 0;
    ParryShellWindow(QWidget *parent = nullptr) : QMainWindow(parent), inputField(nullptr), promptLabel(nullptr), outputBuffer(""), outputColorBuffer(0) {
        setupUI();
        displayWelcomeBanner();
        inputField->setFocus();
    }

private:
    bool eventFilter(QObject *obj, QEvent *event) override {
        if (obj == inputField && event->type() == QEvent::KeyPress) {
            auto keyEvent = dynamic_cast<QKeyEvent *>(event);

            
            if (keyEvent->key() == Qt::Key_C && keyEvent->modifiers() == Qt::MetaModifier) {
                request_interrupt();
                return true; 
            }

            
            if (keyEvent->key() == Qt::Key_C && keyEvent->modifiers() == Qt::ControlModifier) {
                if (outputView->textCursor().hasSelection()) {
                    outputView->copy();
                    return true; 
                }
            }

            
            if (keyEvent->key() == Qt::Key_L && keyEvent->modifiers() == Qt::MetaModifier) {
                outputView->clear();
                return true;
            }

            
            if (keyEvent->key() == Qt::Key_U && keyEvent->modifiers() == Qt::MetaModifier) {
                inputField->clear();
                return true;
            }

            
            if (keyEvent->key() == Qt::Key_W && keyEvent->modifiers() == Qt::MetaModifier) {
                int pos = inputField->cursorPosition();
                QString text = inputField->text();
                int start = text.lastIndexOf(' ', pos - 1);
                if (start == -1) {
                    start = 0;
                } else {
                    start++;
                }
                text.remove(start, pos - start);
                inputField->setText(text);
                inputField->setCursorPosition(start);
                return true;
            }

            
            if (keyEvent->key() == Qt::Key_Home || (keyEvent->key() == Qt::Key_A && keyEvent->modifiers() == Qt::MetaModifier)) {
                inputField->setCursorPosition(0);
                return true;
            }

            
            if (keyEvent->key() == Qt::Key_End || (keyEvent->key() == Qt::Key_E && keyEvent->modifiers() == Qt::MetaModifier)) {
                inputField->setCursorPosition(inputField->text().length());
                return true;
            }

            
            if (keyEvent->key() == Qt::Key_Left && keyEvent->modifiers() == Qt::AltModifier) {
                inputField->cursorBackward(true, 1);
                return true;
            }

            
            if (keyEvent->key() == Qt::Key_Right && keyEvent->modifiers() == Qt::AltModifier) {
                inputField->cursorForward(true, 1);
                return true;
            }

            
            if (keyEvent->key() == Qt::Key_D && keyEvent->modifiers() == Qt::MetaModifier) {
                if (inputField->text().isEmpty()) {
                    QCoreApplication::exit(0);
                }
                return true;
            }

            
            if (keyEvent->key() == Qt::Key_Escape) {
                inputField->clear();
                return true;
            }

            
            if (keyEvent->key() == Qt::Key_PageUp) {
                outputView->verticalScrollBar()->setValue(outputView->verticalScrollBar()->value() - outputView->viewport()->height());
                return true;
            }

            
            if (keyEvent->key() == Qt::Key_PageDown) {
                outputView->verticalScrollBar()->setValue(outputView->verticalScrollBar()->value() + outputView->viewport()->height());
                return true;
            }

            if (keyEvent->key() == Qt::Key_Tab) {
                std::string completion = handle_tab_completion(inputField->text().toStdString());
                if (!completion.empty()) {
                    inputField->insert(QString::fromStdString(completion));
                }
                return true;
            }

            if (keyEvent->key() == Qt::Key_Up) {
                if (!cmd_history.empty() && history_index < cmd_history.size()) {
                    history_index++;
                    std::string cmd = cmd_history[cmd_history.size() - history_index].second;
                    inputField->setText(QString::fromStdString(cmd));
                }
                return true;
            }

            if (keyEvent->key() == Qt::Key_Down) {
                if (history_index > 0) {
                    history_index--;
                    if (history_index == 0) {
                        inputField->clear();
                    } else {
                        std::string cmd = cmd_history[cmd_history.size() - history_index].second;
                        inputField->setText(QString::fromStdString(cmd));
                    }
                }
                return true;
            }
        }


        return QMainWindow::eventFilter(obj, event);
    }

    void setupUI() {
        
        setWindowTitle("ParryShell");
        resize(900, 600);
        setMinimumSize(500, 300);

        
        QWidget *centralWidget = new QWidget(this);
        QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
        mainLayout->setContentsMargins(0, 0, 0, 0);
        mainLayout->setSpacing(0);

        
        outputView = new OutputView(this);
        outputView->setReadOnly(true);
        outputView->setFont(QFont("Menlo", 14));
        outputView->setStyleSheet(QString(
            "QTextEdit {"
            "  background-color: %1;"
            "  color: %2;"
            "  border: none;"
            "  padding: 6px;"
            "}"
        ).arg(BG_COLOR, TEXT_COLOR));

        
        QWidget *inputBar = new QWidget(this);
        inputBar->setFixedHeight(36);
        inputBar->setStyleSheet(QString("background-color: %1;").arg(BG_COLOR));

        QHBoxLayout *inputLayout = new QHBoxLayout(inputBar);
        inputLayout->setContentsMargins(8, 0, 8, 0);
        inputLayout->setSpacing(8);

        
        promptLabel = new QLabel(inputBar);
        promptLabel->setFont(QFont("Menlo", 11));
        promptLabel->setStyleSheet(QString("color: %1;").arg(PROMPT_COLOR));
        updatePromptLabel();

        
        inputField = new QLineEdit(inputBar);
        inputField->setFont(QFont("Menlo", 14));
        inputField->setFrame(false);
        inputField->setStyleSheet(QString(
            "QLineEdit {"
            "  background-color: %1;"
            "  color: %2;"
            "  padding-left: 8px;"
            "}"
        ).arg(BG_COLOR, TEXT_COLOR));

        inputLayout->addWidget(promptLabel);
        inputLayout->addWidget(inputField);

        
        mainLayout->addWidget(outputView, 1); 
        mainLayout->addWidget(inputBar);

        setCentralWidget(centralWidget);


        inputField->installEventFilter(this);
        
        connect(inputField, &QLineEdit::returnPressed, this, &ParryShellWindow::onCommandSubmitted);
        connect(outputView, &OutputView::clicked, this, [this]() {
            inputField->setFocus();
        });
    }

    void displayWelcomeBanner() {
        const QString banner = "ParryShell v1.0 | Type 'help' for commands";

        
        outputView->append(QString("<font color='%1'>%2</font>").arg(PROMPT_COLOR, banner.toHtmlEscaped()));

        QString macOSVersion = QSysInfo::prettyProductName();
        QString kernelVersion = QSysInfo::kernelVersion();
        QString username = qgetenv("USER");
        QString dateTime = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");

        const QString sysInfo = QString("macOS %1 [Darwin %2]  |  User: %3  |  %4\n")
                .arg(macOSVersion, kernelVersion, username, dateTime);

        outputView->append(QString("<font color='%1'>%2</font>").arg(INFO_COLOR, sysInfo.toHtmlEscaped()));
    }

    void onCommandSubmitted() {
        QString command = inputField->text();

        if (command.isEmpty()) {
            return;
        }


        HIS::addHistory(command.toStdString());

        
        
        
        
        QString echo = QString("~ %1").arg(command.toHtmlEscaped());
        outputView->setTextColor(QColor(TEXT_COLOR));
        outputView->append(echo);

        std::string rawCmd = command.toStdString();
        auto tokens = parse_(rawCmd);

        
        outputBuffer = "";
        outputColorBuffer = 0;

        
        handle_(tokens, rawCmd.length(), [this](const std::string &text, int colorCode) {
            
            if (colorCode != outputColorBuffer && !outputBuffer.empty()) {
                
                flushOutputBuffer();
            }
            outputBuffer += text;
            outputColorBuffer = colorCode;
        });

        
        flushOutputBuffer();

        
        updatePromptLabel();

        inputField->clear();
        history_index = 0;
    }

    void flushOutputBuffer() {
        if (outputBuffer.empty()) return;

        QString qtext = QString::fromStdString(outputBuffer);
        int colorCode = outputColorBuffer;
        outputBuffer.clear();
        outputColorBuffer = 0;

        auto doInsert = [this, qtext, colorCode]() {
            
            outputView->moveCursor(QTextCursor::End);

            
            if (colorCode == 3) {
                outputView->clear();
                outputView->setTextColor(QColor(0, 160, 160)); 
                
                outputView->append("~ ");
            } else if (colorCode == 1) {
                outputView->setTextColor(QColor(244, 71, 71)); 
            } else if (colorCode == 2) {
                outputView->setTextColor(QColor(229, 192, 123)); 
            } else if (colorCode == 4) {
                
                outputView->setTextColor(QColor(102, 195, 102));
            } else if (colorCode == 5) {
                
                outputView->setTextColor(QColor(255, 215, 0));
            } else {
                outputView->setTextColor(QColor(204, 204, 204)); 
            }

            
            outputView->insertPlainText(qtext);

            
            outputView->setTextColor(QColor(TEXT_COLOR));

            
            outputView->verticalScrollBar()->setValue(outputView->verticalScrollBar()->maximum());
        };

        
        
        if (QThread::currentThread() == this->thread()) {
            doInsert();
        } else {
            QMetaObject::invokeMethod(this, doInsert, Qt::QueuedConnection);
        }
    }

    void updatePromptLabel() {
        if (promptLabel) {
            char *cwd = getcwd(nullptr, 0);
            QString label = QString::fromStdString(std::string(cwd) + ">");
            promptLabel->setText(label);
            free(cwd);
        }
    }

    OutputView *outputView;
    QLineEdit *inputField;
    QLabel *promptLabel;
    std::string outputBuffer;
    int outputColorBuffer;
};


void WIN() {
    
    
    static int argc = 1;
    char *argv[] = {(char *) "ParryShell", nullptr};

    QApplication app(argc, argv);

    ParryShellWindow window;
    window.show();

    
    app.exec();
}

#include "window.moc"