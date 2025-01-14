/*
    SPDX-FileCopyrightText: 2007 Glenn Ergeerts <glenn.ergeerts@telenet.be>
    SPDX-FileCopyrightText: 2012 Marco Gulino <marco.gulino@xpeppers.com>

    SPDX-License-Identifier: LGPL-2.0-or-later
*/

#include "bookmarkmatch.h"
#include <QVariant>

// TODO: test

BookmarkMatch::BookmarkMatch(const QIcon &icon, const QString &searchTerm, const QString &bookmarkTitle, const QString &bookmarkURL, const QString &description)
    : m_icon(icon)
    , m_searchTerm(searchTerm)
    , m_bookmarkTitle(bookmarkTitle)
    , m_bookmarkURL(bookmarkURL)
    , m_description(description)
{
}

KRunner::QueryMatch BookmarkMatch::asQueryMatch(KRunner::AbstractRunner *runner)
{
    KRunner::QueryMatch::Type type;
    qreal relevance = 0;

    if (m_bookmarkTitle.compare(m_searchTerm, Qt::CaseInsensitive) == 0
        || (!m_description.isEmpty() && m_description.compare(m_searchTerm, Qt::CaseInsensitive) == 0)) {
        type = KRunner::QueryMatch::ExactMatch;
        relevance = 1.0;
    } else if (m_bookmarkTitle.contains(m_searchTerm, Qt::CaseInsensitive)) {
        type = KRunner::QueryMatch::PossibleMatch;
        relevance = 0.45;
    } else if (!m_description.isEmpty() && m_description.contains(m_searchTerm, Qt::CaseInsensitive)) {
        type = KRunner::QueryMatch::PossibleMatch;
        relevance = 0.3;
    } else if (m_bookmarkURL.contains(m_searchTerm, Qt::CaseInsensitive)) {
        type = KRunner::QueryMatch::PossibleMatch;
        relevance = 0.2;
    } else {
        type = KRunner::QueryMatch::PossibleMatch;
        relevance = 0.18;
    }

    bool isNameEmpty = m_bookmarkTitle.isEmpty();
    bool isDescriptionEmpty = m_description.isEmpty();

    KRunner::QueryMatch match(runner);
    match.setType(type);
    match.setRelevance(relevance);
    match.setIcon(m_icon);
    match.setSubtext(m_bookmarkURL);

    // Try to set the following as text in this order: name, description, url
    match.setText(isNameEmpty ? (!isDescriptionEmpty ? m_description : m_bookmarkURL) : m_bookmarkTitle);

    match.setData(m_bookmarkURL);
    match.setUrls({QUrl(m_bookmarkURL)});
    return match;
}

void BookmarkMatch::addTo(QList<BookmarkMatch> &listOfResults, bool addEvenOnNoMatch)
{
    if (!addEvenOnNoMatch && !(matches(m_searchTerm, m_bookmarkTitle) || matches(m_searchTerm, m_description) || matches(m_searchTerm, m_bookmarkURL))) {
        return;
    }
    listOfResults << *this;
}

bool BookmarkMatch::matches(const QString &search, const QString &matchingField)
{
    return !matchingField.simplified().isEmpty() && matchingField.contains(search, Qt::CaseInsensitive);
}
