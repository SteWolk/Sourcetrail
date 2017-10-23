#include "qt/element/QtCodeArea.h"

#include <algorithm>

#include <QApplication>
#include <QFont>
#include <QHBoxLayout>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QTextBlock>
#include <QToolTip>

#include "data/location/SourceLocationFile.h"
#include "utility/messaging/type/MessageActivateLocalSymbols.h"
#include "utility/messaging/type/MessageFocusIn.h"
#include "utility/messaging/type/MessageFocusOut.h"
#include "utility/messaging/type/MessageMoveIDECursor.h"
#include "utility/messaging/type/MessageShowErrors.h"
#include "utility/utility.h"
#include "utility/utilityString.h"

#include "qt/element/QtCodeNavigator.h"
#include "qt/utility/QtContextMenu.h"
#include "settings/ColorScheme.h"

MouseWheelOverScrollbarFilter::MouseWheelOverScrollbarFilter()
{
}

bool MouseWheelOverScrollbarFilter::eventFilter(QObject* obj, QEvent* event)
{
	QScrollBar* scrollbar = dynamic_cast<QScrollBar*>(obj);
	if (event->type() == QEvent::Wheel && scrollbar)
	{
		QRect scrollbarArea(scrollbar->pos(), scrollbar->size());
		QPoint globalMousePos = dynamic_cast<QWheelEvent*>(event)->globalPos();
		QPoint localMousePos = scrollbar->mapFromGlobal(globalMousePos);

		// instead of "scrollbar->underMouse()" we need this check implemented here because "underMouse()"
		// does not work when the mouse enters the area without being moved
		if (scrollbarArea.contains(localMousePos))
		{
			event->ignore();
			return true;
		}
	}
	return QObject::eventFilter(obj, event);
}

QtLineNumberArea::QtLineNumberArea(QtCodeArea *codeArea)
	: QWidget(codeArea)
	, m_codeArea(codeArea)
{
	setObjectName("line_number_area");
}

QtLineNumberArea::~QtLineNumberArea()
{
}

QSize QtLineNumberArea::sizeHint() const
{
	return QSize(m_codeArea->lineNumberAreaWidth(), 0);
}

void QtLineNumberArea::paintEvent(QPaintEvent *event)
{
	m_codeArea->lineNumberAreaPaintEvent(event);
}


QtCodeArea::QtCodeArea(
	uint startLineNumber,
	const std::string& code,
	std::shared_ptr<SourceLocationFile> locationFile,
	QtCodeNavigator* navigator,
	bool showLineNumbers,
	QWidget* parent
)
	: QtCodeField(startLineNumber, code, locationFile, parent)
	, m_navigator(navigator)
	, m_digits(0)
	, m_isSelecting(false)
	, m_isPanning(false)
	, m_setIDECursorPositionAction(nullptr)
	, m_eventPosition(0, 0)
	, m_isActiveFile(false)
	, m_showLineNumbers(showLineNumbers)
{
	setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);

	m_lineNumberArea = new QtLineNumberArea(this);

	m_digits = lineNumberDigits();
	updateLineNumberAreaWidth();

	connect(this, &QtCodeArea::blockCountChanged, this, &QtCodeArea::updateLineNumberAreaWidth);
	connect(this, &QtCodeArea::updateRequest, this, &QtCodeArea::updateLineNumberArea);

	// MouseWheelOverScrollbarFilter is deleted by parent.
	horizontalScrollBar()->installEventFilter(new MouseWheelOverScrollbarFilter());
	m_scrollSpeedChangeListener.setScrollBar(horizontalScrollBar());

	createActions();
}

QtCodeArea::~QtCodeArea()
{
	if (m_setIDECursorPositionAction != nullptr)
	{
		m_setIDECursorPositionAction->disconnect();
		delete m_setIDECursorPositionAction;
	}
}

QSize QtCodeArea::sizeHint() const
{
	double height = 0;
	double width = 0;

	for (QTextBlock block = document()->firstBlock(); block.isValid(); block = block.next())
	{
		QRectF rect = blockBoundingGeometry(block);
		height += rect.height();
		width = std::max(rect.width(), width);
	}

	int scrollHeight = 0;
	if (horizontalScrollBar()->minimum() != horizontalScrollBar()->maximum())
	{
		scrollHeight = horizontalScrollBar()->height();
	}

	return QSize(width + lineNumberAreaWidth() + 1, height + scrollHeight + 5);
}

void QtCodeArea::lineNumberAreaPaintEvent(QPaintEvent *event)
{
	QPainter painter(m_lineNumberArea);

	QTextBlock block = firstVisibleBlock();
	int blockNumber = block.blockNumber();
	int top = static_cast<int>(blockBoundingGeometry(block).translated(contentOffset()).top());
	int bottom = top + static_cast<int>(blockBoundingRect(block).height());

	std::set<int> activeLineNumbers;
	std::set<int> focusedLineNumbers;

	std::set<Id> activeSymbolIds = m_navigator->getActiveTokenIds();
	std::set<Id> activeLocationIds = m_navigator->getCurrentActiveLocationIds();

	for (const Annotation& annotation : m_annotations)
	{
		bool focus = false;
		bool active = false;

		switch (annotation.locationType)
		{
		case LOCATION_LOCAL_SYMBOL:
			if (annotation.isActive || annotation.isFocused)
			{
				focus = true;
			}
			break;

		case LOCATION_ERROR:
		case LOCATION_SCREEN_SEARCH:
			if (annotation.isFocused || annotation.isActive)
			{
				focus = true;
			}
			else
			{
				active = true;
			}
			break;

		case LOCATION_TOKEN:
		case LOCATION_SCOPE:
			if (annotation.isActive && activeLocationIds.size())
			{
				focus = true;
				break;
			}
			else if (annotation.isFocused && utility::shareElement(activeSymbolIds, annotation.tokenIds))
			{
				active = true;
				break;
			}

		default:
			if (annotation.isActive)
			{
				active = true;
			}
			else if (annotation.isFocused)
			{
				focus = true;
			}
			break;
		}

		if (active || focus)
		{
			for (int i = annotation.startLine; i <= annotation.endLine; i++)
			{
				if (active)
				{
					activeLineNumbers.insert(i);
				}
				else
				{
					focusedLineNumbers.insert(i);
				}
			}
		}
	}

	ColorScheme* scheme = ColorScheme::getInstance().get();

	QColor textColor(scheme->getColor("code/snippet/line_number/text").c_str());
	QColor inactiveTextColor(scheme->getColor("code/snippet/line_number/inactive_text").c_str());
	QColor activeMarkerColor(scheme->getColor("code/snippet/line_number/marker/active").c_str());
	QColor focusedMarkerColor(scheme->getColor("code/snippet/line_number/marker/focus").c_str());

	QPen p = painter.pen();

	while (block.isValid() && top <= event->rect().bottom())
	{
		if (block.isVisible() && bottom >= event->rect().top())
		{
			int number = blockNumber + getStartLineNumber();

			p.setColor(textColor);

			if (focusedLineNumbers.find(number) != focusedLineNumbers.end())
			{
				painter.fillRect(m_lineNumberArea->width() - 8, top, 3, fontMetrics().height() + 1, focusedMarkerColor);
			}
			else if (activeLineNumbers.find(number) != activeLineNumbers.end())
			{
				painter.fillRect(m_lineNumberArea->width() - 8, top, 3, fontMetrics().height() + 1, activeMarkerColor);
			}
			else if (!m_isActiveFile)
			{
				p.setColor(inactiveTextColor);
			}

			painter.setPen(p);
			painter.drawText(
				0, top, m_lineNumberArea->width() - 16, fontMetrics().height(), Qt::AlignRight, QString::number(number));
		}

		block = block.next();
		top = bottom;
		bottom = top + static_cast<int>(blockBoundingRect(block).height());
		blockNumber++;
	}
}

int QtCodeArea::lineNumberDigits() const
{
	int max = qMax(1, int(getStartLineNumber()) + blockCount());
	return utility::digits(max);
}

int QtCodeArea::lineNumberAreaWidth() const
{
	if (m_showLineNumbers)
	{
		return fontMetrics().width(QLatin1Char('9')) * m_digits + 30;
	}

	return 0;
}

void QtCodeArea::updateLineNumberAreaWidthForDigits(int digits)
{
	m_digits = digits;
	updateLineNumberAreaWidth();
}

void QtCodeArea::updateContent()
{
	annotateText();
}

void QtCodeArea::setIsActiveFile(bool isActiveFile)
{
	m_isActiveFile = isActiveFile;
}

uint QtCodeArea::getLineNumberForLocationId(Id locationId) const
{
	for (const Annotation& annotation : m_annotations)
	{
		if (annotation.locationId == locationId)
		{
			return annotation.startLine;
		}
	}

	return 0;
}

std::pair<uint, uint> QtCodeArea::getLineNumbersForLocationId(Id locationId) const
{
	for (const Annotation& annotation : m_annotations)
	{
		if (annotation.locationId == locationId)
		{
			return std::pair<uint, uint>(annotation.startLine, annotation.endLine);
		}
	}

	return std::pair<uint, uint>(0, 0);
}

Id QtCodeArea::getLocationIdOfFirstActiveLocation(Id tokenId) const
{
	for (const Annotation& annotation : m_annotations)
	{
		if (annotation.locationType == LocationType::LOCATION_TOKEN && annotation.isActive &&
			annotation.tokenIds.find(tokenId) != annotation.tokenIds.end())
		{
			return annotation.locationId;
		}
	}

	return 0;
}

Id QtCodeArea::getLocationIdOfFirstActiveScopeLocation(Id tokenId) const
{
	for (const Annotation& annotation : m_annotations)
	{
		if (annotation.locationType == LocationType::LOCATION_SCOPE && annotation.isActive &&
			annotation.tokenIds.find(tokenId) != annotation.tokenIds.end())
		{
			return annotation.locationId;
		}
	}

	return 0;
}

uint QtCodeArea::getActiveLocationCount() const
{
	uint count = 0;

	for (const Annotation& annotation : m_annotations)
	{
		if (annotation.locationType == LocationType::LOCATION_TOKEN && (annotation.isActive || annotation.isFocused))
		{
			count++;
		}
	}

	return count;
}

QRectF QtCodeArea::getLineRectForLineNumber(uint lineNumber) const
{
	if (lineNumber < getStartLineNumber())
	{
		lineNumber = getStartLineNumber();
	}
	else if (lineNumber > getEndLineNumber())
	{
		lineNumber = getEndLineNumber();
	}

	QTextBlock block = document()->findBlockByLineNumber(lineNumber - getStartLineNumber());
	return blockBoundingGeometry(block);
}

void QtCodeArea::findScreenMatches(const std::string& query, std::vector<std::pair<QtCodeArea*, Id>>* screenMatches)
{
	const std::string& code = utility::toLowerCase(getCode());
	size_t pos = 0;
	while (pos != std::string::npos)
	{
		pos = code.find(query, pos);
		if (pos == std::string::npos)
		{
			break;
		}

		Annotation matchAnnotation;
		matchAnnotation.start = pos;
		matchAnnotation.end = pos + query.size();

		std::pair<int, int> start = toLineColumn(matchAnnotation.start);
		matchAnnotation.startLine = start.first;
		matchAnnotation.startCol = start.second;

		std::pair<int, int> end = toLineColumn(matchAnnotation.end);
		matchAnnotation.endLine = end.first;
		matchAnnotation.endCol = end.second;

		// Set first 2 bits to 1 to avoid collisions
		matchAnnotation.locationId = ~(~Id(0) >> 2) + screenMatches->size() + 1;
		matchAnnotation.locationType = LOCATION_SCREEN_SEARCH;

		matchAnnotation.isActive = false;
		matchAnnotation.isFocused = false;

		m_annotations.push_back(matchAnnotation);
		screenMatches->push_back(std::make_pair(this, matchAnnotation.locationId));

		pos += query.size();
	}

	if (screenMatches->size() && screenMatches->back().first == this)
	{
		viewport()->update();
	}
}

void QtCodeArea::clearScreenMatches()
{
	size_t i = m_annotations.size();
	while (i > 0 && m_annotations[i - 1].locationType == LOCATION_SCREEN_SEARCH)
	{
		i--;
		m_linesToRehighlight.push_back(m_annotations[i].startLine - getStartLineNumber());
	}

	if (i != m_annotations.size())
	{
		m_annotations.erase(m_annotations.begin() + i, m_annotations.end());
		viewport()->update();
	}
}

void QtCodeArea::resizeEvent(QResizeEvent *e)
{
	QPlainTextEdit::resizeEvent(e);

	QRect cr = contentsRect();
	m_lineNumberArea->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
}

void QtCodeArea::mousePressEvent(QMouseEvent* event)
{
	clearSelection();

	if (event->button() == Qt::LeftButton)
	{
		m_oldMousePosition = event->pos();
		m_panningDistance = 0;

		if (Qt::KeyboardModifier::ShiftModifier & QApplication::keyboardModifiers())
		{
			m_isPanning = true;
			viewport()->setCursor(Qt::ClosedHandCursor);
		}
		else
		{
			m_isSelecting = true;
			QTextCursor cursor = this->cursorForPosition(event->pos());
			setNewTextCursor(cursor);

			viewport()->setCursor(Qt::IBeamCursor);
		}
	}
}

void QtCodeArea::mouseReleaseEvent(QMouseEvent* event)
{
	const int panningThreshold = 5;
	if (event->button() == Qt::LeftButton)
	{
		m_isSelecting = false;
		m_isPanning = false;

		viewport()->setCursor(Qt::ArrowCursor);

		if (m_panningDistance < panningThreshold) // dont do anything if mouse is release to end some real panning action.
		{
			if (Qt::KeyboardModifier::ControlModifier & QApplication::keyboardModifiers())
			{
				m_eventPosition = event->pos();
				setIDECursorPosition();
			}
			else
			{
				QTextCursor cursor = this->cursorForPosition(event->pos());
				std::vector<const Annotation*> annotations = getInteractiveAnnotationsForPosition(cursor.position());

				if (annotations.size())
				{
					if (m_navigator->hasErrors())
					{
						activateErrors(annotations);
					}
					else
					{
						activateAnnotations(annotations);
					}
				}
				else if (m_navigator->getActiveLocalSymbolIds().size())
				{
					MessageActivateLocalSymbols(std::vector<Id>()).dispatch();
				}
			}
		}
	}
}

void QtCodeArea::mouseMoveEvent(QMouseEvent* event)
{
	const QPoint currentMousePosition = event->pos();
	const int deltaX = currentMousePosition.x() - m_oldMousePosition.x();
	const int deltaY = currentMousePosition.y() - m_oldMousePosition.y();
	m_oldMousePosition = currentMousePosition;
	m_panningDistance += abs(deltaX + deltaY);

	if (m_isSelecting)
	{
		QTextCursor cursor = textCursor();
		cursor.setPosition(this->cursorForPosition(event->pos()).position(), QTextCursor::KeepAnchor);
		setNewTextCursor(cursor);
	}
	else if (m_isPanning)
	{
		QScrollBar* scrollbar = horizontalScrollBar();
		int visibleContentWidth = width() - lineNumberAreaWidth();
		float deltaPosRatio = float(deltaX) / (visibleContentWidth);
		scrollbar->setValue(scrollbar->value() - utility::roundToInt(deltaPosRatio * scrollbar->pageStep()));
	}

	QTextCursor cursor = this->cursorForPosition(event->pos());
	std::vector<const Annotation*> annotations = getInteractiveAnnotationsForPosition(cursor.position());

	bool same = annotations.size() == m_hoveredAnnotations.size();
	if (same)
	{
		for (size_t i = 0; i < annotations.size(); i++)
		{
			if (annotations[i] != m_hoveredAnnotations[i])
			{
				same = false;
				break;
			}
		}
	}

	if (!same)
	{
		QToolTip::hideText();

		setHoveredAnnotations(annotations);

		if (m_navigator->hasErrors() && annotations.size() == 1 && annotations[0]->tokenIds.size())
		{
			std::string errorMessage = m_navigator->getErrorMessageForId(*annotations[0]->tokenIds.begin());
			QToolTip::showText(event->globalPos(), QString::fromStdString(errorMessage));
		}
	}
}

void QtCodeArea::wheelEvent(QWheelEvent *event)
{
	if ((event->angleDelta().x() != 0 && horizontalScrollBar()->minimum() != horizontalScrollBar()->maximum()) ||
		(event->angleDelta().y() != 0 && verticalScrollBar()->minimum() != verticalScrollBar()->maximum()))
	{
		QPlainTextEdit::wheelEvent(event);
	}
	else
	{
		event->ignore();
	}
}

void QtCodeArea::contextMenuEvent(QContextMenuEvent* event)
{
	if (m_setIDECursorPositionAction != nullptr)
	{
		m_eventPosition = event->pos();

		QtContextMenu menu(event, this);
		if (!getSourceLocationFile()->getFilePath().empty())
		{
			menu.addSeparator();
			menu.addFileActions(getSourceLocationFile()->getFilePath());
			menu.addSeparator();
			menu.addAction(m_setIDECursorPositionAction);
		}
		menu.show();
	}
}

void QtCodeArea::focusTokenIds(const std::vector<Id>& tokenIds)
{
	if (m_navigator->hasErrors() && tokenIds.size() == 1)
	{
		QtCodeField::focusTokenIds(tokenIds);
		return;
	}

	MessageFocusIn(tokenIds, TOOLTIP_ORIGIN_CODE).dispatch();
}

void QtCodeArea::defocusTokenIds(const std::vector<Id>& tokenIds)
{
	if (m_navigator->hasErrors() && tokenIds.size() == 1)
	{
		QtCodeField::defocusTokenIds(tokenIds);
		return;
	}

	MessageFocusOut(tokenIds).dispatch();
}

void QtCodeArea::updateLineNumberAreaWidth(int /* newBlockCount */)
{
	setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void QtCodeArea::updateLineNumberArea(const QRect &rect, int dy)
{
	if (dy)
	{
		m_lineNumberArea->scroll(0, dy);
	}
	else
	{
		m_lineNumberArea->update(0, rect.y(), m_lineNumberArea->width(), rect.height());
	}

	if (rect.contains(viewport()->rect()))
	{
		updateLineNumberAreaWidth();
	}
}

void QtCodeArea::clearSelection()
{
	QTextCursor cursor = textCursor();
	cursor.clearSelection();

	setNewTextCursor(cursor);
}

void QtCodeArea::setNewTextCursor(const QTextCursor& cursor)
{
	int horizontalValue = horizontalScrollBar()->value();
	int verticalValue = verticalScrollBar()->value();

	setTextCursor(cursor);

	horizontalScrollBar()->setValue(horizontalValue);
	verticalScrollBar()->setValue(verticalValue);
}

void QtCodeArea::setIDECursorPosition()
{
	std::pair<int, int> lineColumn = toLineColumn(this->cursorForPosition(m_eventPosition).position());

	MessageMoveIDECursor(getSourceLocationFile()->getFilePath().str(), lineColumn.first, lineColumn.second).dispatch();
}

void QtCodeArea::activateErrors(const std::vector<const Annotation*>& annotations)
{
	std::vector<Id> errorIds;
	for (const Annotation* annotation : annotations)
	{
		if (annotation->locationType == LOCATION_ERROR && annotation->tokenIds.size())
		{
			errorIds.insert(errorIds.end(), annotation->tokenIds.begin(), annotation->tokenIds.end());
		}
	}

	if (errorIds.size() == 1)
	{
		MessageShowErrors(errorIds[0]).dispatch();
	}
}

void QtCodeArea::annotateText()
{
	std::set<Id> activeSymbolIds = m_navigator->getCurrentActiveTokenIds();
	utility::append(activeSymbolIds, m_navigator->getActiveLocalSymbolIds());

	const std::set<Id>& activeLocationIds = m_navigator->getCurrentActiveLocationIds();

	std::set<Id> focusedSymbolIds = m_navigator->getActiveTokenIds();
	for (Id currentActiveId : activeSymbolIds)
	{
		focusedSymbolIds.erase(currentActiveId);
	}
	utility::append(focusedSymbolIds, m_navigator->getFocusedTokenIds());

	bool needsUpdate = QtCodeField::annotateText(activeSymbolIds, activeLocationIds, focusedSymbolIds);
	if (needsUpdate)
	{
		m_lineNumberArea->update();
	}
}

void QtCodeArea::createActions()
{
	m_setIDECursorPositionAction = new QAction(tr("Show in IDE (Ctrl + Left Click)"), this);
#if defined(Q_OS_MAC)
	m_setIDECursorPositionAction->setText(tr("Show in IDE (Cmd + Left Click)"));
#endif
	m_setIDECursorPositionAction->setStatusTip(tr("Set the IDE Cursor to this code position"));
	m_setIDECursorPositionAction->setToolTip(tr("Set the IDE Cursor to this code position"));
	connect(m_setIDECursorPositionAction, &QAction::triggered, this, &QtCodeArea::setIDECursorPosition);
}
