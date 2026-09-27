<%@ WebHandler Language="C#" Class="Example.Handler" %>
using System.Collections.Generic;
using System.Web;
public class Handler : IHttpHandler {
    public void ProcessRequest(HttpContext context) {
        var values = new List<int>();
    }
    public bool IsReusable { get { return false; } }
}
