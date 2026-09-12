using System.ComponentModel;
namespace CyWinTask;

public sealed record TreeEntry(ProcessRow Row, int Depth, int Descendants, bool Expanded, string RootCategory);
public static class ProcessTree
{
    // A parent PID is accepted only when its current creation time precedes the child.
    // Visible applications start their own branch instead of nesting under the desktop shell.
    public static List<TreeEntry> Build(IEnumerable<ProcessRow> source, HashSet<(int, long)> expanded,
        string query, string category, string sortProperty, ListSortDirection direction, ISet<(int,long)>? searchCollapsed = null)
    {
        var rows = source.ToArray();
        var byId = rows.ToDictionary(r => r.Id);
        var parents = new Dictionary<ProcessRow, ProcessRow>();
        foreach (var row in rows)
            if (row.Category != "Applications" && row.Sample.ParentId != row.Id && byId.TryGetValue(row.Sample.ParentId, out var parent)
                && parent.Sample.Created > 0 && row.Sample.Created > 0 && parent.Sample.Created <= row.Sample.Created)
                parents[row] = parent;
        foreach (var row in rows)
        {
            var visited = new HashSet<ProcessRow>();
            var current = row;
            while (parents.TryGetValue(current, out var parent))
            {
                if (!visited.Add(current)) { parents.Remove(current); break; }
                current = parent;
            }
        }
        var children = rows.ToDictionary(r => r, _ => new List<ProcessRow>());
        var roots = new List<ProcessRow>();
        foreach (var row in rows)
            if (parents.TryGetValue(row, out var parent)) children[parent].Add(row); else roots.Add(row);
        int Compare(ProcessRow a, ProcessRow b)
        {
            int result = sortProperty switch
            {
                nameof(ProcessRow.Id) => a.Id.CompareTo(b.Id),
                nameof(ProcessRow.Cpu) => a.Cpu.CompareTo(b.Cpu),
                nameof(ProcessRow.MemoryMb) => a.MemoryMb.CompareTo(b.MemoryMb),
                nameof(ProcessRow.Threads) => a.Threads.CompareTo(b.Threads),
                nameof(ProcessRow.Handles) => a.Handles.CompareTo(b.Handles),
                _ => StringComparer.CurrentCultureIgnoreCase.Compare(a.Name, b.Name)
            };
            if (direction == ListSortDirection.Descending) result = -result;
            if (result == 0) result = StringComparer.CurrentCultureIgnoreCase.Compare(a.Name, b.Name);
            return result == 0 ? a.Id.CompareTo(b.Id) : result;
        }
        roots.Sort(Compare); foreach (var list in children.Values) list.Sort(Compare);
        var traversal = new List<(ProcessRow Row, int Depth, string Category)>();
        var stack = new Stack<(ProcessRow Row, int Depth, string Category)>();
        foreach (var root in roots.AsEnumerable().Reverse()) stack.Push((root,0,root.Category));
        while (stack.TryPop(out var entry))
        {
            traversal.Add(entry);
            foreach (var child in children[entry.Row].AsEnumerable().Reverse()) stack.Push((child,entry.Depth+1,entry.Category));
        }
        var count = rows.ToDictionary(r => r, _ => 0);
        var matches = rows.ToDictionary(r => r, r => MainWindow.Matches(r.Sample, query));
        foreach (var entry in traversal.AsEnumerable().Reverse())
            if (parents.TryGetValue(entry.Row, out var parent)) { count[parent] += 1 + count[entry.Row]; matches[parent] |= matches[entry.Row]; }
        bool searching = query.Length > 0;
        var shown = new Dictionary<ProcessRow,bool>();
        var inheritedMatch = new Dictionary<ProcessRow,bool>();
        var result = new List<TreeEntry>();
        foreach (var entry in traversal)
        {
            bool hasParent = parents.TryGetValue(entry.Row, out var parent);
            bool ancestorMatch = hasParent && inheritedMatch[parent!];
            bool ownMatch = searching && MainWindow.Matches(entry.Row.Sample, query);
            inheritedMatch[entry.Row] = ancestorMatch || ownMatch;
            bool branchMatch = !searching || matches[entry.Row] || ancestorMatch;
            bool isExpanded = (expanded.Contains((entry.Row.Id, entry.Row.Sample.Created)) || searching) && !(searching && searchCollapsed?.Contains((entry.Row.Id, entry.Row.Sample.Created)) == true);
            bool isShown = (category.Length == 0 || entry.Category == category) && branchMatch && (!hasParent || shown[parent!]);
            shown[entry.Row] = isShown && isExpanded;
            if (isShown) result.Add(new(entry.Row,entry.Depth,count[entry.Row],isExpanded,entry.Category));
        }
        return result;
    }
}
