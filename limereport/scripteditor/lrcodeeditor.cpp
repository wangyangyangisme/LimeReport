#include "lrcodeeditor.h"

#include <QAbstractItemView>
#include <QWidget>
#include <QCompleter>
#include <QKeyEvent>
#include <QScrollBar>
#include <QPainter>
#include <QTextBlock>
#include <QDebug>

#include "lrscripthighlighter.h"

namespace LimeReport{

CodeEditor::CodeEditor(QWidget *parent)
    : QPlainTextEdit(parent), m_compleater(0)
{
    lineNumberArea = new LineNumberArea(this);

    connect(this, SIGNAL(blockCountChanged(int)), this, SLOT(updateLineNumberAreaWidth(int)));
    connect(this, SIGNAL(updateRequest(QRect,int)), this, SLOT(updateLineNumberArea(QRect,int)));
    connect(this, SIGNAL(cursorPositionChanged()), this, SLOT(highlightCurrentLine()));

    updateLineNumberAreaWidth(0);
    highlightCurrentLine();
    new ScriptHighlighter(document());
    connect(this, SIGNAL(cursorPositionChanged()),
            this, SLOT(matchParentheses()));
}

void CodeEditor::setCompleter(QCompleter *value)
{
    if (value) disconnect(value,0,this,0);
    m_compleater = value;
    if (!m_compleater) return;
    m_compleater->setWidget(this);
    m_compleater->setCompletionMode(QCompleter::PopupCompletion);
    m_compleater->setCaseSensitivity(Qt::CaseInsensitive);
    connect(m_compleater,SIGNAL(activated(QString)),this,SLOT(insertCompletion(QString)));
}

void CodeEditor::lineNumberAreaPaintEvent(QPaintEvent* event)
{
    QPainter painter(lineNumberArea);
    QStyleOption option;
    option.initFrom(this);
    //painter.fillRect(event->rect(), QPalette().background().color());
    QColor bg = option.palette.background().color().darker(150);
    painter.fillRect(event->rect(), bg);

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = (int) blockBoundingGeometry(block).translated(contentOffset()).top();
    int bottom = top + (int) blockBoundingRect(block).height();

    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            QString number = QString::number(blockNumber + 1);
            painter.setPen(option.palette.text().color());
            painter.drawText(0, top, lineNumberArea->width(), fontMetrics().height(),
                             Qt::AlignCenter, number);
        }

        block = block.next();
        top = bottom;
        bottom = top + (int) blockBoundingRect(block).height();
        ++blockNumber;
    }
}

int CodeEditor::lineNumberAreaWidth()
{
    int digits = 1;
    int max = qMax(1, blockCount());
    while (max >= 10) {
        max /= 10;
        ++digits;
    }

    int space = fontMetrics().width(QLatin1Char('9'))*2 + fontMetrics().width(QLatin1Char('9')) * digits;

    return space;
}

void CodeEditor::keyPressEvent(QKeyEvent *e)
{
    if (m_compleater && m_compleater->popup()->isVisible()) {
        switch (e->key()) {
        case Qt::Key_Enter:
        case Qt::Key_Return:
        case Qt::Key_Escape:
        case Qt::Key_Tab:
        case Qt::Key_Backtab:
            e->ignore();
            return;
        default:
            break;
        }
    }

    bool isShortcut = ((e->modifiers() & Qt::ControlModifier) && e->key() == Qt::Key_Space);
    if (!m_compleater || !isShortcut) QPlainTextEdit::keyPressEvent(e);

    const bool ctrlOrShift = e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier);
    if (!m_compleater || (ctrlOrShift && e->text().isEmpty()))
        return;

    static QString eow("~!@#$%^&*()_+{}|:\"<>?,./;'[]\\-="); // end of word
    bool hasModifier = (e->modifiers() != Qt::NoModifier) && !ctrlOrShift;

    QString completionPrefix = textUnderCursor();

    if (!isShortcut && (hasModifier || e->text().isEmpty()|| completionPrefix.length() < 3
                        || eow.contains(e->text().right(1)))) {
        m_compleater->popup()->hide();
        return;
    }

    if (completionPrefix != m_compleater->completionPrefix()) {
        m_compleater->setCompletionPrefix(completionPrefix);
        m_compleater->popup()->setCurrentIndex(m_compleater->completionModel()->index(0, 0));
    }

    QRect cr = cursorRect();
    cr.setWidth(m_compleater->popup()->sizeHintForColumn(0)
                + m_compleater->popup()->verticalScrollBar()->sizeHint().width());
    m_compleater->complete(cr);

}

void CodeEditor::focusInEvent(QFocusEvent *e)
{
    if (m_compleater) m_compleater->setWidget(this);
    QPlainTextEdit::focusInEvent(e);
}

void CodeEditor::resizeEvent(QResizeEvent* event)
{
    QPlainTextEdit::resizeEvent(event);
    QRect cr = contentsRect();
    lineNumberArea->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
}

QString CodeEditor::textUnderCursor() const
{
    QTextCursor tc = textCursor();
    tc.select(QTextCursor::WordUnderCursor);
    return tc.selectedText();
}

bool CodeEditor::matchLeftParenthesis(QTextBlock currentBlock, QChar parenthesisType, int i, int numLeftParentheses)
{
    TextBlockData *data = static_cast<TextBlockData *>(currentBlock.userData());
    if (data){
        QVector<ParenthesisInfo *> infos = data->parentheses();

        int docPos = currentBlock.position();
        for (; i < infos.size(); ++i) {
            ParenthesisInfo *info = infos.at(i);

            if (info->character == parenthesisType) {
                ++numLeftParentheses;
                continue;
            }

            if (info->character == getParenthesisReverceChar(parenthesisType)){
                if (numLeftParentheses == 0) {
                    createParenthesisSelection(docPos + info->position);
                    return true;
                } else
                    --numLeftParentheses;
            }

        }

        currentBlock = currentBlock.next();
        if (currentBlock.isValid())
            return matchLeftParenthesis(currentBlock, parenthesisType, 0, numLeftParentheses);
    }

    return false;
}

bool CodeEditor::matchRightParenthesis(QTextBlock currentBlock, QChar parenthesisType, int i, int numRightParentheses)
{
    TextBlockData *data = static_cast<TextBlockData *>(currentBlock.userData());
    if (data){
        QVector<ParenthesisInfo *> parentheses = data->parentheses();
        int docPos = currentBlock.position();
        if (i == -2) i = parentheses.size()-1;
        for (; i > -1 && parentheses.size() > 0; --i) {
            ParenthesisInfo *info = parentheses.at(i);
            if (info->character == parenthesisType) {
                ++numRightParentheses;
                continue;
            }
            if (info->character == getParenthesisReverceChar(parenthesisType)){
                if (numRightParentheses == 0) {
                    createParenthesisSelection(docPos + info->position);
                    return true;
                } else
                    --numRightParentheses;
            }
        }

        currentBlock = currentBlock.previous();
        if (currentBlock.isValid())
            return matchRightParenthesis(currentBlock, parenthesisType, -2, numRightParentheses);

    }
    return false;
}

void CodeEditor::createParenthesisSelection(int pos)
{
    QList<QTextEdit::ExtraSelection> selections = extraSelections();

    QTextEdit::ExtraSelection selection;
    QTextCharFormat format = selection.format;
    format.setBackground(QColor("#619934"));
    format.setForeground(QColor("#ffffff"));
    selection.format = format;

    QTextCursor cursor = textCursor();
    cursor.setPosition(pos);
    cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
    selection.cursor = cursor;

    selections.append(selection);

    setExtraSelections(selections);
}

bool CodeEditor::charIsParenthesis(QChar character, ParenthesisType type)
{
    for (int i = 0; i < PARENHEIS_COUNT; ++i){
        if (character == parenthesisCharacters[type][i])
            return true;
    }
    return false;
}

QChar CodeEditor::getParenthesisReverceChar(QChar parenthesisChar)
{
    for (int i = 0; i < PARENHEIS_COUNT; ++i){
        if ( parenthesisCharacters[RightParenthesis][i] == parenthesisChar)
            return parenthesisCharacters[LeftParenthesis][i];
        if ( parenthesisCharacters[LeftParenthesis][i] == parenthesisChar)
            return parenthesisCharacters[RightParenthesis][i];
    }
    return ' ';
}

void CodeEditor::insertCompletion(const QString &completion)
{
    if (m_compleater->widget() != this)
             return;
    QTextCursor tc = textCursor();
    int extra = completion.length() - m_compleater->completionPrefix().length();
    tc.movePosition(QTextCursor::Left);
    tc.movePosition(QTextCursor::EndOfWord);
    tc.insertText(completion.right(extra));
    setTextCursor(tc);
}

void CodeEditor::updateLineNumberAreaWidth(int /*newBlockCount*/)
{
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void CodeEditor::highlightCurrentLine()
{
    QList<QTextEdit::ExtraSelection> extraSelections;

    if (!isReadOnly()) {
        QTextEdit::ExtraSelection selection;

        QColor lineColor = QColor(QPalette().background().color()).darker(100);

        selection.format.setBackground(lineColor);
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        selection.cursor = textCursor();
        selection.cursor.clearSelection();
        extraSelections.append(selection);
    }

    setExtraSelections(extraSelections);
}

void CodeEditor::updateLineNumberArea(const QRect& rect, int dy)
{
    if (dy)
        lineNumberArea->scroll(0, dy);
    else
        lineNumberArea->update(0, rect.y(), lineNumberArea->width(), rect.height());

    if (rect.contains(viewport()->rect()))
        updateLineNumberAreaWidth(0);
}

void CodeEditor::matchParentheses()
{
    QList<QTextEdit::ExtraSelection> selections;
    setExtraSelections(selections);

    TextBlockData *data = static_cast<TextBlockData *>(textCursor().block().userData());

    if (data) {
        QVector<ParenthesisInfo *> infos = data->parentheses();

        int pos = textCursor().block().position();
        for (int i = 0; i < infos.size(); ++i) {
            ParenthesisInfo *info = infos.at(i);

            int curPos = textCursor().position() - textCursor().block().position();

            if ( (info->position == (curPos - 1)) && charIsParenthesis(info->character, LeftParenthesis))
            {
                if (matchLeftParenthesis(textCursor().block(), info->character, i + 1, 0))
                    createParenthesisSelection(pos + info->position);
            } else if ( (info->position == (curPos - 1)) && charIsParenthesis(info->character, RightParenthesis)) {
                if (matchRightParenthesis(textCursor().block(), info->character, i - 1, 0))
                    createParenthesisSelection(pos + info->position);
            }
         }
    }
}

} //namespace LimeReport
