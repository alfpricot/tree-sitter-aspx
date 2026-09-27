<%@ WebService Language="C#" Class="Example.Service" %>
<script runat="server">
public class Service : System.Web.Services.WebService {
    [System.Web.Services.WebMethod]
    public string Ping() { return "ok"; }
}
</script>
