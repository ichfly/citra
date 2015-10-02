// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <QMouseEvent>
#include <QPainter>
#include <QString>

#include "citra_qt/debugger/profiler.h"
#include "citra_qt/util/util.h"

#include "common/microprofile.h"
#include "common/profiler_reporting.h"

// Include the implementation of the UI in this file. This isn't in microprofile.cpp because the
// non-Qt frontends don't need it (and don't implement the UI drawing hooks either).
#define MICROPROFILEUI_IMPL 1
#include "common/microprofileui.h"

using namespace Common::Profiling;

static QVariant GetDataForColumn(int col, const AggregatedDuration& duration)
{
    static auto duration_to_float = [](Duration dur) -> float {
        using FloatMs = std::chrono::duration<float, std::chrono::milliseconds::period>;
        return std::chrono::duration_cast<FloatMs>(dur).count();
    };

    switch (col) {
    case 1: return duration_to_float(duration.avg);
    case 2: return duration_to_float(duration.min);
    case 3: return duration_to_float(duration.max);
    default: return QVariant();
    }
}

static const TimingCategoryInfo* GetCategoryInfo(int id)
{
    const auto& categories = GetProfilingManager().GetTimingCategoriesInfo();
    if ((size_t)id >= categories.size()) {
        return nullptr;
    } else {
        return &categories[id];
    }
}

ProfilerModel::ProfilerModel(QObject* parent) : QAbstractItemModel(parent)
{
    updateProfilingInfo();
    const auto& categories = GetProfilingManager().GetTimingCategoriesInfo();
    results.time_per_category.resize(categories.size());
}

QVariant ProfilerModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        switch (section) {
        case 0: return tr("Category");
        case 1: return tr("Avg");
        case 2: return tr("Min");
        case 3: return tr("Max");
        }
    }

    return QVariant();
}

QModelIndex ProfilerModel::index(int row, int column, const QModelIndex& parent) const
{
    return createIndex(row, column);
}

QModelIndex ProfilerModel::parent(const QModelIndex& child) const
{
    return QModelIndex();
}

int ProfilerModel::columnCount(const QModelIndex& parent) const
{
    return 4;
}

int ProfilerModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) {
        return 0;
    } else {
        return static_cast<int>(results.time_per_category.size() + 2);
    }
}

QVariant ProfilerModel::data(const QModelIndex& index, int role) const
{
    if (role == Qt::DisplayRole) {
        if (index.row() == 0) {
            if (index.column() == 0) {
                return tr("Frame");
            } else {
                return GetDataForColumn(index.column(), results.frame_time);
            }
        } else if (index.row() == 1) {
            if (index.column() == 0) {
                return tr("Frame (with swapping)");
            } else {
                return GetDataForColumn(index.column(), results.interframe_time);
            }
        } else {
            if (index.column() == 0) {
                const TimingCategoryInfo* info = GetCategoryInfo(index.row() - 2);
                return info != nullptr ? QString(info->name) : QVariant();
            } else {
                if (index.row() - 2 < (int)results.time_per_category.size()) {
                    return GetDataForColumn(index.column(), results.time_per_category[index.row() - 2]);
                } else {
                    return QVariant();
                }
            }
        }
    }

    return QVariant();
}

void ProfilerModel::updateProfilingInfo()
{
    results = GetTimingResultsAggregator()->GetAggregatedResults();
    emit dataChanged(createIndex(0, 1), createIndex(rowCount() - 1, 3));
}

ProfilerWidget::ProfilerWidget(QWidget* parent) : QDockWidget(parent)
{
    ui.setupUi(this);

    model = new ProfilerModel(this);
    ui.treeView->setModel(model);

    connect(this, SIGNAL(visibilityChanged(bool)), SLOT(setProfilingInfoUpdateEnabled(bool)));
    connect(&update_timer, SIGNAL(timeout()), model, SLOT(updateProfilingInfo()));
}

void ProfilerWidget::setProfilingInfoUpdateEnabled(bool enable)
{
    if (enable) {
        update_timer.start(100);
        model->updateProfilingInfo();
    } else {
        update_timer.stop();
    }
}

class MicroProfileWidget : public QWidget {
public:
    MicroProfileWidget(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* ev) override;
    void showEvent(QShowEvent* ev) override;
    void hideEvent(QHideEvent* ev) override;

    void mouseMoveEvent(QMouseEvent* ev) override;
    void mousePressEvent(QMouseEvent* ev) override;
    void mouseReleaseEvent(QMouseEvent* ev) override;
    void wheelEvent(QWheelEvent* ev) override;

    void keyPressEvent(QKeyEvent* ev) override;
    void keyReleaseEvent(QKeyEvent* ev) override;

private:
    /// This timer is used to redraw the widget's contents continuously. To save resources, it only
    /// runs while the widget is visible.
    QTimer update_timer;
};

MicroProfileDialog::MicroProfileDialog(QWidget* parent)
    : QWidget(parent, Qt::Dialog)
{
    setObjectName("MicroProfile");
    setWindowTitle(tr("MicroProfile"));
    resize(1000, 600);
    // Remove the "?" button from the titlebar and enable the maximize button
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint | Qt::WindowMaximizeButtonHint);

    MicroProfileWidget* widget = new MicroProfileWidget(this);

    QLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(widget);
    setLayout(layout);

    // Configure focus so that widget is focusable and the dialog automatically forwards focus to it.
    setFocusProxy(widget);
    widget->setFocusPolicy(Qt::StrongFocus);
    widget->setFocus();
}

QAction* MicroProfileDialog::toggleViewAction() {
    if (toggle_view_action == nullptr) {
        toggle_view_action = new QAction(windowTitle(), this);
        toggle_view_action->setCheckable(true);
        toggle_view_action->setChecked(isVisible());
        connect(toggle_view_action, SIGNAL(toggled(bool)), SLOT(setVisible(bool)));
    }

    return toggle_view_action;
}

void MicroProfileDialog::showEvent(QShowEvent* ev) {
    if (toggle_view_action) {
        toggle_view_action->setChecked(isVisible());
    }
    QWidget::showEvent(ev);
}

void MicroProfileDialog::hideEvent(QHideEvent* ev) {
    if (toggle_view_action) {
        toggle_view_action->setChecked(isVisible());
    }
    QWidget::hideEvent(ev);
}

/// There's no way to pass a user pointer to MicroProfile, so this variable is used to make the
/// QPainter available inside the drawing callbacks.
static QPainter* mp_painter = nullptr;

MicroProfileWidget::MicroProfileWidget(QWidget* parent) : QWidget(parent) {
    // Send mouse motion events even when not dragging.
    setMouseTracking(true);

    MicroProfileSetDisplayMode(1); // Timers screen
    MicroProfileInitUI();

    connect(&update_timer, SIGNAL(timeout()), SLOT(update()));
}

void MicroProfileWidget::paintEvent(QPaintEvent* ev) {
    QPainter painter(this);

    painter.setBackground(Qt::black);
    painter.eraseRect(rect());

    QFont font = GetMonospaceFont();
    font.setPixelSize(MICROPROFILE_TEXT_HEIGHT);
    painter.setFont(font);

    mp_painter = &painter;
    MicroProfileDraw(rect().width(), rect().height());
    mp_painter = nullptr;
}

void MicroProfileWidget::showEvent(QShowEvent* ev) {
    update_timer.start(15); // ~60 Hz
    QWidget::showEvent(ev);
}

void MicroProfileWidget::hideEvent(QHideEvent* ev) {
    update_timer.stop();
    QWidget::hideEvent(ev);
}

void MicroProfileWidget::mouseMoveEvent(QMouseEvent* ev) {
    MicroProfileMousePosition(ev->x(), ev->y(), 0);
    ev->accept();
}

void MicroProfileWidget::mousePressEvent(QMouseEvent* ev) {
    MicroProfileMousePosition(ev->x(), ev->y(), 0);
    MicroProfileMouseButton(ev->buttons() & Qt::LeftButton, ev->buttons() & Qt::RightButton);
    ev->accept();
}

void MicroProfileWidget::mouseReleaseEvent(QMouseEvent* ev) {
    MicroProfileMousePosition(ev->x(), ev->y(), 0);
    MicroProfileMouseButton(ev->buttons() & Qt::LeftButton, ev->buttons() & Qt::RightButton);
    ev->accept();
}

void MicroProfileWidget::wheelEvent(QWheelEvent* ev) {
    MicroProfileMousePosition(ev->x(), ev->y(), ev->delta() / 120);
    ev->accept();
}

void MicroProfileWidget::keyPressEvent(QKeyEvent* ev) {
    if (ev->key() == Qt::Key_Control) {
        // Inform MicroProfile that the user is holding Ctrl.
        MicroProfileModKey(1);
    }
    QWidget::keyPressEvent(ev);
}

void MicroProfileWidget::keyReleaseEvent(QKeyEvent* ev) {
    if (ev->key() == Qt::Key_Control) {
        MicroProfileModKey(0);
    }
    QWidget::keyReleaseEvent(ev);
}

// These functions are called by MicroProfileDraw to draw the interface elements on the screen.

void MicroProfileDrawText(int x, int y, u32 hex_color, const char* text, u32 text_length) {
    // hex_color does not include an alpha, so it must be assumed to be 255
    mp_painter->setPen(QColor::fromRgb(hex_color));

    // It's impossible to draw a string using a monospaced font with a fixed width per cell in a
    // way that's reliable across different platforms and fonts as far as I (yuriks) can tell, so
    // draw each character individually in order to precisely control the text advance.
    for (u32 i = 0; i < text_length; ++i) {
        // Position the text baseline 1 pixel above the bottom of the text cell, this gives nice
        // vertical alignment of text for a wide range of tested fonts.
        mp_painter->drawText(x, y + MICROPROFILE_TEXT_HEIGHT - 2, QChar(text[i]));
        x += MICROPROFILE_TEXT_WIDTH + 1;
    }
}

void MicroProfileDrawBox(int left, int top, int right, int bottom, u32 hex_color, MicroProfileBoxType type) {
    QColor color = QColor::fromRgba(hex_color);
    QBrush brush = color;
    if (type == MicroProfileBoxTypeBar) {
        QLinearGradient gradient(left, top, left, bottom);
        gradient.setColorAt(0.f, color.lighter(125));
        gradient.setColorAt(1.f, color.darker(125));
        brush = gradient;
    }
    mp_painter->fillRect(left, top, right - left, bottom - top, brush);
}

void MicroProfileDrawLine2D(u32 vertices_length, float* vertices, u32 hex_color) {
    // Temporary vector used to convert between the float array and QPointF. Marked static to reuse
    // the allocation across calls.
    static std::vector<QPointF> point_buf;

    for (u32 i = 0; i < vertices_length; ++i) {
        point_buf.emplace_back(vertices[i*2 + 0], vertices[i*2 + 1]);
    }

    // hex_color does not include an alpha, so it must be assumed to be 255
    mp_painter->setPen(QColor::fromRgb(hex_color));
    mp_painter->drawPolyline(point_buf.data(), vertices_length);
    point_buf.clear();
}