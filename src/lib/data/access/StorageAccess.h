#ifndef STORAGE_ACCESS_H
#define STORAGE_ACCESS_H

#include <memory>
#include <string>
#include <vector>

#include "utility/types.h"
#include "utility/file/FileInfo.h"
#include "utility/file/FilePath.h"

#include "data/bookmark/Bookmark.h"
#include "data/bookmark/BookmarkCategory.h"
#include "data/bookmark/EdgeBookmark.h"
#include "data/bookmark/NodeBookmark.h"
#include "data/graph/Node.h"
#include "data/search/SearchMatch.h"
#include "data/ErrorCountInfo.h"
#include "data/ErrorFilter.h"
#include "data/ErrorInfo.h"
#include "data/StorageStats.h"

class Graph;
class SourceLocationCollection;
class SourceLocationFile;
class TextAccess;

class StorageAccess
{
public:
	virtual ~StorageAccess();

	virtual Id getNodeIdForFileNode(const FilePath& filePath) const = 0;
	virtual Id getNodeIdForNameHierarchy(const NameHierarchy& nameHierarchy) const = 0;
	virtual std::vector<Id> getNodeIdsForNameHierarchies(const std::vector<NameHierarchy> nameHierarchies) const = 0;

	virtual NameHierarchy getNameHierarchyForNodeId(Id id) const = 0;
	virtual std::vector<NameHierarchy> getNameHierarchiesForNodeIds(const std::vector<Id> nodeIds) const = 0;

	virtual Node::NodeType getNodeTypeForNodeWithId(Id id) const = 0;

	virtual Id getIdForEdge(
		Edge::EdgeType type, const NameHierarchy& fromNameHierarchy, const NameHierarchy& toNameHierarchy) const = 0;
	virtual StorageEdge getEdgeById(Id edgeId) const = 0;

	virtual std::shared_ptr<SourceLocationCollection> getFullTextSearchLocations(
			const std::string& searchTerm, bool caseSensitive) const = 0;
	virtual std::vector<SearchMatch> getAutocompletionMatches(const std::string& query) const = 0;
	virtual std::vector<SearchMatch> getSearchMatchesForTokenIds(const std::vector<Id>& tokenIds) const = 0;

	virtual std::shared_ptr<Graph> getGraphForAll() const = 0;
	virtual std::shared_ptr<Graph> getGraphForActiveTokenIds(const std::vector<Id>& tokenIds, bool* isActiveNamespace = nullptr) const = 0;
	virtual std::shared_ptr<Graph> getGraphForTrail(Id originId, Id targetId, Edge::EdgeTypeMask trailType, size_t depth) const = 0;

	virtual std::vector<Id> getActiveTokenIdsForId(Id tokenId, Id* declarationId) const = 0;
	virtual std::vector<Id> getNodeIdsForLocationIds(const std::vector<Id>& locationIds) const = 0;

	virtual std::shared_ptr<SourceLocationCollection> getSourceLocationsForTokenIds(
		const std::vector<Id>& tokenIds) const = 0;
	virtual std::shared_ptr<SourceLocationCollection> getSourceLocationsForLocationIds(
		const std::vector<Id>& locationIds) const = 0;
	virtual std::shared_ptr<SourceLocationFile> getSourceLocationsForFile(const FilePath& filePath) const = 0;
	virtual std::shared_ptr<SourceLocationFile> getSourceLocationsForLinesInFile(
		const std::string& filePath, uint firstLineNumber, uint lastLineNumber) const = 0;

	virtual std::shared_ptr<SourceLocationFile> getCommentLocationsInFile(const FilePath& filePath) const = 0;

	virtual std::shared_ptr<TextAccess> getFileContent(const FilePath& filePath) const = 0;

	virtual FileInfo getFileInfoForFilePath(const FilePath& filePath) const = 0;
	virtual std::vector<FileInfo> getFileInfosForFilePaths(const std::vector<FilePath>& filePaths) const = 0;

	virtual StorageStats getStorageStats() const = 0;

	virtual ErrorCountInfo getErrorCount() const = 0;
	virtual std::vector<ErrorInfo> getErrors() const = 0;

	virtual std::shared_ptr<SourceLocationCollection> getErrorSourceLocations(std::vector<ErrorInfo>* errors) const = 0;

	virtual void setErrorFilter(const ErrorFilter& filter);

	virtual Id addNodeBookmark(const NodeBookmark& bookmark) = 0; // todo: remove these from storage access
	virtual Id addEdgeBookmark(const EdgeBookmark& bookmark) = 0; // todo: remove these from storage access
	virtual Id addBookmarkCategory(const std::string& categoryName) = 0; // todo: remove these from storage access

	virtual void updateBookmark(const Id bookmarkId, const std::string& name, const std::string& comment, const std::string& categoryName) = 0; // todo: remove these from storage access

	virtual void removeBookmark(const Id id) = 0; // todo: remove these from storage access
	virtual void removeBookmarkCategory(const Id id) = 0; // todo: remove these from storage access

	virtual std::vector<NodeBookmark> getAllNodeBookmarks() const = 0;
	virtual std::vector<EdgeBookmark> getAllEdgeBookmarks() const = 0;

	virtual std::vector<BookmarkCategory> getAllBookmarkCategories() const = 0;

protected:
	ErrorFilter m_errorFilter;
};

#endif // STORAGE_ACCESS_H
