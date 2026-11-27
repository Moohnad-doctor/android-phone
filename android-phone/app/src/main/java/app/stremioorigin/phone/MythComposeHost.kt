package app.stremioorigin.phone

import android.view.ViewGroup
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.activity.compose.setContent
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.ComposeView
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.platform.ViewCompositionStrategy
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

object MythComposeHost {
    @JvmStatic
    fun attach(activity: MainActivity, container: ViewGroup) {
        val host = ComposeView(activity).apply {
            layoutDirection = android.view.View.LAYOUT_DIRECTION_RTL
            setViewCompositionStrategy(ViewCompositionStrategy.DisposeOnViewTreeLifecycleDestroyed)
            setContent {
                MythShell(onLegacySurface = activity::showLegacySurface)
            }
        }
        container.addView(
            host,
            ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT
            )
        )
        activity.setComposeHost(host)
    }
}

private data class MythTab(val label: String, val title: String, val subtitle: String)

@Composable
private fun MythShell(onLegacySurface: () -> Unit) {
    val tabs = remember {
        listOf(
            MythTab("الرئيسية", "مرحباً بك", "واجهة Myth الجديدة — مساحة رئيسية للتصفح واكتشاف المحتوى."),
            MythTab("التنزيلات", "التنزيلات", "ستظهر التنزيلات وحالة تقدمها هنا بعد ربط محرك التنزيلات."),
            MythTab("البحث", "البحث", "ابحث عن الأفلام والمسلسلات — سيتم تفعيل البحث في المرحلة التالية."),
            MythTab("الإضافات", "الإضافات", "إدارة إضافات Stremio ومصادر المحتوى ستتوفر في المرحلة التالية."),
            MythTab("الإعدادات", "الإعدادات", "إعدادات الواجهة والتشغيل ستظهر هنا.")
        )
    }
    var selectedTab by remember { mutableIntStateOf(0) }

    CompositionLocalProvider(LocalLayoutDirection provides LayoutDirection.Rtl) {
        MaterialTheme {
            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .background(Color.Black)
                    .padding(horizontal = 20.dp, vertical = 18.dp)
            ) {
                Spacer(Modifier.height(26.dp))
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .horizontalScroll(rememberScrollState())
                        .clip(RoundedCornerShape(40.dp))
                        .background(Color(0xFF111111))
                        .border(1.dp, Color(0xFF292929), RoundedCornerShape(40.dp))
                        .padding(5.dp),
                    horizontalArrangement = Arrangement.spacedBy(3.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    tabs.forEachIndexed { index, tab ->
                        val active = selectedTab == index
                        Box(
                            modifier = Modifier
                                .clip(RoundedCornerShape(32.dp))
                                .background(if (active) Color(0xFF292929) else Color.Transparent)
                                .clickable { selectedTab = index }
                                .padding(horizontal = 15.dp, vertical = 12.dp),
                            contentAlignment = Alignment.Center
                        ) {
                            Text(
                                text = tab.label,
                                color = if (active) Color.White else Color(0xFFAAAAAA),
                                fontSize = 13.sp,
                                fontWeight = if (active) FontWeight.SemiBold else FontWeight.Medium,
                                maxLines = 1
                            )
                        }
                    }
                }

                Spacer(Modifier.height(46.dp))
                Text(
                    text = tabs[selectedTab].title,
                    color = Color.White,
                    fontSize = 30.sp,
                    fontWeight = FontWeight.Bold,
                    textAlign = TextAlign.Start
                )
                Spacer(Modifier.height(12.dp))
                Text(
                    text = tabs[selectedTab].subtitle,
                    color = Color(0xFF999999),
                    fontSize = 15.sp,
                    lineHeight = 25.sp,
                    textAlign = TextAlign.Start
                )

                Spacer(Modifier.height(28.dp))
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clip(RoundedCornerShape(20.dp))
                        .background(Color(0xFF101010))
                        .border(1.dp, Color(0xFF242424), RoundedCornerShape(20.dp))
                        .padding(22.dp)
                ) {
                    Column {
                        Text(
                            text = when (selectedTab) {
                                0 -> "اكتشف عالمك"
                                1 -> "لا توجد تنزيلات للعرض"
                                2 -> "ابدأ البحث"
                                3 -> "لا توجد إضافات معروضة"
                                else -> "تخصيص تجربتك"
                            },
                            color = Color.White,
                            fontSize = 17.sp,
                            fontWeight = FontWeight.SemiBold
                        )
                        Spacer(Modifier.height(8.dp))
                        Text(
                            text = "هذه شاشة واجهة أولية للتحقق من Compose وRTL والوضع الأسود الحقيقي.",
                            color = Color(0xFF777777),
                            fontSize = 13.sp,
                            lineHeight = 22.sp
                        )
                    }
                }

                Spacer(Modifier.weight(1f))
                Box(
                    modifier = Modifier
                        .align(Alignment.CenterHorizontally)
                        .clip(RoundedCornerShape(30.dp))
                        .background(Color(0xFF1C1C1C))
                        .border(1.dp, Color(0xFF343434), RoundedCornerShape(30.dp))
                        .clickable(onClick = onLegacySurface)
                        .padding(horizontal = 22.dp, vertical = 13.dp),
                    contentAlignment = Alignment.Center
                ) {
                    Text(
                        text = "إظهار واجهة SDL القديمة / إخفاء Compose",
                        color = Color(0xFFE0E0E0),
                        fontSize = 13.sp,
                        fontWeight = FontWeight.Medium
                    )
                }
                Spacer(Modifier.height(14.dp))
            }
        }
    }
}
