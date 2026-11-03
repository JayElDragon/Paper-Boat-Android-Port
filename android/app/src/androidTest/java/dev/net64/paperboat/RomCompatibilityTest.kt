package dev.net64.paperboat

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Test
import org.junit.Assert.*
import org.junit.runner.RunWith
import java.io.File
import java.security.MessageDigest
import org.json.JSONArray
import org.json.JSONObject

@RunWith(AndroidJUnit4::class)
class RomCompatibilityTest {
    @Test fun verifyProvidedRegionalDumpsAndExtractSupportedUsa() {
        val context=InstrumentationRegistry.getInstrumentation().targetContext
        assertNull("Bundled resources must stage",GameAssets.stageBundledAssets(context))
        val directory=File(GameAssets.gameDir(context),"compat_tests")
        val cases=listOf(
            Triple("USA","USA.z64","3837f44cda784b466c9a2d99df70d77c322b97a0"),
            Triple("JPN","JPN.n64","b9cca3ff260b9ff427d981626b82f96de73586d3"),
            Triple("EUR","EUR.n64","2111d39265a317414d359e35a7d971c4dfa5f9e1"))
        val report=JSONArray()
        for((region,name,expectedHash) in cases) {
            val source=File(directory,name);assertTrue("Missing input $name",source.isFile)
            val copy=File(directory,"normalized-$region.z64");source.copyTo(copy,true)
            GameAssets.normalizeRom(copy)
            val hash=MessageDigest.getInstance("SHA-1").digest(copy.readBytes()).joinToString(""){"%02x".format(it)}
            assertEquals("Regional dump checksum $region",expectedHash,hash)
            val identified=GameAssets.identifyRom(context,copy)
            val row=JSONObject().put("region",region).put("sha1",hash).put("normalized",true)
                .put("recognized",identified!=null).put("recipe",identified ?: JSONObject.NULL)
            report.put(row)
            File(GameAssets.gameDir(context),"compatibility-report.json").writeText(report.toString(2))
            if(region=="USA") {
                assertNotNull("USA recipe must recognize the verified dump",identified)
                copy.copyTo(GameAssets.romFile(context),true)
            } else assertNull("No extraction recipe is implemented for $region",identified)
        }
        val error=GameAssets.generateGameArchive(context)
        report.getJSONObject(0).put("extractionError",error ?: JSONObject.NULL)
            .put("archiveBytes",GameAssets.gameArchive(context).length())
        File(GameAssets.gameDir(context),"compatibility-report.json").writeText(report.toString(2))
        assertNull("USA extraction must complete",error)
        assertTrue("Extracted archive must be nonempty",GameAssets.isExtracted(context))
    }
}
