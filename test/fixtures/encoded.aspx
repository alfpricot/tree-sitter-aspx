<%@ Page Title="About" Language="VB" MasterPageFile="~/Site.Master" Inherits="Example.About" %>
<asp:Content ID="Body" ContentPlaceHolderID="Main" runat="server">
    <h2><%: Title %>.</h2>
    <p>&copy; <%: DateTime.Now.Year %> - Example</p>
    <%-- server comment is not code --%>
</asp:Content>
