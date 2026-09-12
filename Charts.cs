using System.Globalization;
using System.Windows;
using System.Windows.Media;
namespace CyWinTask;
public sealed class HistoryChart : FrameworkElement
{
    public MetricHistory History { get; }
    public Brush Accent { get; set; } = Brushes.LightSkyBlue;
    public List<(MetricHistory History, Brush Color)> AdditionalSeries { get; } = new();
    public IEnumerable<(MetricHistory History, Brush Color)> Series => new[] { (History, Accent) }.Concat(AdditionalSeries);
    public HistoryChart(MetricHistory history) { History = history; Height = 150; ClipToBounds = true; }
    public double Scale(string unit)
    {
        var series = Series.Where(s => s.History.Metric.Unit == unit).ToArray();
        var fixedMax = series.Select(s => s.History.Metric.Maximum ?? 0).Max();
        return fixedMax > 0 ? fixedMax : Math.Max(1, series.SelectMany(s => s.History.Points).Select(p => p.Value ?? 0).DefaultIfEmpty(0).Max() * 1.15);
    }
    protected override void OnRender(DrawingContext dc)
    {
        base.OnRender(dc);
        double w = ActualWidth, h = ActualHeight;
        if (w < 4 || h < 4) return;
        var grid = new Pen(new SolidColorBrush(Color.FromRgb(43, 59, 72)), 1);
        dc.DrawRectangle(new SolidColorBrush(Color.FromRgb(27,32,39)), grid, new Rect(0,0,w,h));
        for(int i=1;i<12;i++) dc.DrawLine(grid,new(w*i/12,0),new(w*i/12,h));
        for(int i=1;i<5;i++) dc.DrawLine(grid,new(0,h*i/5),new(w,h*i/5));
        var all = Series.ToArray();
        DateTime end = all.SelectMany(s => s.History.Points).Select(p => p.Time).DefaultIfEmpty(DateTime.UtcNow).Max();
        foreach(var series in all)
        {
            double max = Scale(series.History.Metric.Unit);
            var geometry = new StreamGeometry();
            using(var path=geometry.Open())
            {
                bool started=false; DateTime? previous=null;
                foreach(var point in series.History.Points)
                {
                    if(point.Value is not double value) { started=false; previous=null; continue; }
                    double x=w*(1-(end-point.Time).TotalSeconds/60);
                    if(x<0) continue;
                    var position=new Point(x,h-2-Math.Clamp(value/max,0,1)*(h-4));
                    if(previous.HasValue && (point.Time-previous.Value).TotalSeconds>3.5) started=false;
                    if(!started) { path.BeginFigure(position,false,false); started=true; } else path.LineTo(position,true,false);
                    previous=point.Time;
                }
            }
            geometry.Freeze(); dc.DrawGeometry(null,new Pen(series.Color,1.6),geometry);
        }
        var units=all.Select(s=>s.History.Metric.Unit).Distinct().ToArray();
        for(int i=0;i<units.Length;i++)
        {
            var label=new FormattedText($"{Scale(units[i]):N1} {units[i]}",CultureInfo.CurrentCulture,FlowDirection.LeftToRight,new Typeface("Segoe UI"),10,Brushes.LightSlateGray,VisualTreeHelper.GetDpi(this).PixelsPerDip);
            dc.DrawText(label,new Point(units.Length>1 && i==0 ? 6 : Math.Max(0,w-label.Width-6),4));
        }
    }
}
public sealed class UsageCell : FrameworkElement
{
    public static readonly DependencyProperty PercentProperty=DependencyProperty.Register(nameof(Percent),typeof(double),typeof(UsageCell),new FrameworkPropertyMetadata(0d,FrameworkPropertyMetadataOptions.AffectsRender));
    public static readonly DependencyProperty LabelProperty=DependencyProperty.Register(nameof(Label),typeof(string),typeof(UsageCell),new FrameworkPropertyMetadata("",FrameworkPropertyMetadataOptions.AffectsRender));
    public static readonly DependencyProperty IsMemoryProperty=DependencyProperty.Register(nameof(IsMemory),typeof(bool),typeof(UsageCell),new FrameworkPropertyMetadata(false,FrameworkPropertyMetadataOptions.AffectsRender));
    public double Percent { get=>(double)GetValue(PercentProperty); set=>SetValue(PercentProperty,value); }
    public string Label { get=>(string)GetValue(LabelProperty); set=>SetValue(LabelProperty,value); }
    public bool IsMemory { get=>(bool)GetValue(IsMemoryProperty); set=>SetValue(IsMemoryProperty,value); }
    protected override Size MeasureOverride(Size size)=>new(double.IsInfinity(size.Width)?100:size.Width,23);
    public static Color Heat(double percent,bool memory)
    {
        double score=memory?percent*5:percent;
        return score>=50?Color.FromRgb(248,111,106):score>=20?Color.FromRgb(245,174,82):score>=5?Color.FromRgb(230,202,102):Color.FromRgb(99,202,210);
    }
    protected override void OnRender(DrawingContext dc)
    {
        double p=double.IsFinite(Percent)?Math.Clamp(Percent,0,100):0;
        double w=ActualWidth,h=ActualHeight; if(w<=0||h<=0)return;
        Color color=Heat(p,IsMemory);
        dc.DrawRoundedRectangle(new SolidColorBrush(Color.FromArgb((byte)(p>0?48:12),color.R,color.G,color.B)),null,new Rect(0,0,w,h),3,3);
        dc.DrawRectangle(new SolidColorBrush(color),null,new Rect(0,Math.Max(0,h-3),w*p/100,Math.Min(3,h)));
        var text=new FormattedText(Label,CultureInfo.CurrentCulture,FlowDirection.LeftToRight,new Typeface("Segoe UI"),11,Brushes.White,VisualTreeHelper.GetDpi(this).PixelsPerDip){MaxTextWidth=Math.Max(1,w-8),Trimming=TextTrimming.CharacterEllipsis};
        dc.DrawText(text,new Point(4,Math.Max(0,(h-text.Height)/2-1)));
    }
}
