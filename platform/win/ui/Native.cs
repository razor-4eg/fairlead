//===============================================================
//
// copyright (c) 2026 razor-4eg.
//
// Native.cs -- The fairlead-core.dll calls the window uses.
//
//===============================================================

using System.Runtime.InteropServices;
using System.Text;

namespace Fairlead;

static class Core {
    const string Dll = "fairlead-core.dll";

/*
===============
fl_abi_inspect
===============
*/
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    static extern IntPtr fl_abi_inspect([MarshalAs(UnmanagedType.LPUTF8Str)] string config);

/*
===============
fl_abi_apply
===============
*/
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    static extern IntPtr fl_abi_apply([MarshalAs(UnmanagedType.LPUTF8Str)] string config, [MarshalAs(UnmanagedType.LPUTF8Str)] string fields);

/*
===============
fl_abi_blank
===============
*/
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    static extern IntPtr fl_abi_blank();

/*
===============
fl_abi_keypair
===============
*/
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    static extern int fl_abi_keypair(byte[] priv, int privCap, byte[] pub, int pubCap);

/*
===============
fl_abi_import
===============
*/
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    static extern int fl_abi_import([MarshalAs(UnmanagedType.LPUTF8Str)] string text, byte[] config, int configCap, byte[] name, int nameCap);

/*
===============
fl_abi_classify
===============
*/
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    static extern int fl_abi_classify([MarshalAs(UnmanagedType.LPUTF8Str)] string line, byte[] kind, int kindCap, byte[] value, int valueCap, byte[] note, int noteCap, byte[] error, int errorCap);

/*
===============
fl_abi_free
===============
*/
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    static extern void fl_abi_free(IntPtr p);

    /*
    ===============
    Take
    A DLL string. The DLL frees it.
    ===============
    */
    static string Take(IntPtr p) {
        if (p == IntPtr.Zero) {
            return "";
        }

        string text = Marshal.PtrToStringUTF8(p) ?? "";
        fl_abi_free(p);
        return text;
    }

    /*
    ===============
    Bytes
    UTF-8 up to the first zero.
    ===============
    */
    static string Bytes(byte[] buf) {
        int n = Array.IndexOf(buf, (byte)0);
        if (n < 0) {
            n = buf.Length;
        }

        return Encoding.UTF8.GetString(buf, 0, n);
    }

    /*
    ===============
    Inspect
    The fields and the summary for one config.
    ===============
    */
    public static string Inspect(string config) => Take(fl_abi_inspect(config ?? ""));

    /*
    ===============
    Apply
    Patch a config from the editor fields.
    ===============
    */
    public static string Apply(string config, string fields) => Take(fl_abi_apply(config ?? "", fields ?? "{}"));

    /*
    ===============
    Blank
    A new config.
    ===============
    */
    public static string Blank() => Take(fl_abi_blank());

    /*
    ===============
    Keypair
    A new private and public key.
    ===============
    */
    public static (string priv, string pub) Keypair() {
        var priv = new byte[160];
        var pub = new byte[160];
        fl_abi_keypair(priv, priv.Length, pub, pub.Length);
        return (Bytes(priv), Bytes(pub));
    }

    /*
    ===============
    Import
    Pasted text to a config and a name.
    ===============
    */
    public static (int code, string config, string name) Import(string text) {
        var config = new byte[49152];
        var name = new byte[256];
        int code = fl_abi_import(text ?? "", config, config.Length, name, name.Length);
        return (code, Bytes(config), Bytes(name));
    }

    /*
    ===============
    Classify
    One route line.
    ===============
    */
    public static (string kind, string value, string note, string error) Classify(string line) {
        var kind = new byte[32];
        var value = new byte[256];
        var note = new byte[256];
        var error = new byte[256];
        fl_abi_classify(line ?? "", kind, kind.Length, value, value.Length, note, note.Length, error, error.Length);
        return (Bytes(kind), Bytes(value), Bytes(note), Bytes(error));
    }
}
